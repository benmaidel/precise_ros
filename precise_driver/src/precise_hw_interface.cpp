#include <precise_driver/precise_hw_interface.h>
#include <angles/angles.h>

#include <controller_manager_msgs/SwitchController.h>

namespace precise_driver
{
    PreciseHWInterface::PreciseHWInterface(ros::NodeHandle &nh, urdf::Model *urdf_model)
        : ros_control_boilerplate::GenericHWInterface(nh, urdf_model)
    {
        ros::NodeHandle pnh(nh_, "hardware_interface");
        ros::NodeHandle driver_nh(nh_, "driver");

        std::string ip;
        pnh.param<std::string>("ip_address", ip, ip);
        int control_port = 10100;
        pnh.param<int>("control_port", control_port, control_port);
        int status_port = 10000;
        pnh.param<int>("status_port", status_port, status_port);
        pnh.param<int>("profile_no", _profile_no, _profile_no);
        pnh.param<int>("speed", _profile.speed, _profile.speed);
        pnh.param<int>("speed2", _profile.speed2, _profile.speed2);
        pnh.param<int>("accel", _profile.accel, _profile.accel);
        pnh.param<int>("decel", _profile.decel, _profile.decel);
        pnh.param<double>("accel_ramp", _profile.accel_ramp, _profile.accel_ramp);
        pnh.param<double>("decel_ramp", _profile.decel_ramp, _profile.decel_ramp);
        pnh.param<int>("in_range", _profile.in_range, _profile.in_range);
        pnh.param<int>("straight", _profile.straight, _profile.straight);

        _device.reset(new PFlexDevice(std::make_shared<PreciseTCPInterface>(ip, control_port),
                                std::make_shared<PreciseTCPInterface>(ip, status_port)));

        _init_srv = driver_nh.advertiseService("init", &PreciseHWInterface::initCb, this);
        _teachmode_srv = driver_nh.advertiseService("teach_mode", &PreciseHWInterface::teachmodeCb, this);
        _home_srv = driver_nh.advertiseService("home", &PreciseHWInterface::homeCb, this);
        _power_srv = driver_nh.advertiseService("power", &PreciseHWInterface::powerCb, this);
        _cmd_srv = driver_nh.advertiseService("command", &PreciseHWInterface::cmdCb, this);
        _grasp_plate_srv = driver_nh.advertiseService("grasp_plate", &PreciseHWInterface::graspPlateCB, this);
        _release_plate_srv = driver_nh.advertiseService("release_plate", &PreciseHWInterface::releasePlateCB, this);
        _gripper_srv = driver_nh.advertiseService("gripper", &PreciseHWInterface::gripperCB, this);

        _switch_controller_srv = nh_.serviceClient<controller_manager_msgs::SwitchController>("controller_manager/switch_controller");
    }

    PreciseHWInterface::~PreciseHWInterface()
    {
    }

    void PreciseHWInterface::init()
    {
        //Wait for hardware to init
        std::unique_lock<std::mutex> lock(_mutex_init);
        ROS_INFO("Waiting for robot init");
        _cond_init.wait(lock);

        GenericHWInterface::init();
        std::vector<double> joints = _device->getJointPositions();

        for(size_t i = 0; i < num_joints_; i++)
        {
            joint_position_[i] = joints[i];
        }
        ROS_INFO("PreciseHWInterface Ready.");
    }

    void PreciseHWInterface::read(ros::Duration &elapsed_time)
    {
        std::vector<double> joints = _device->getJointPositions();

        for(size_t i = 0; i < num_joints_; i++)
        {
            joint_position_[i] = joints[i];
        }
    }

    void PreciseHWInterface::write(ros::Duration &elapsed_time)
    {
        // Safety
        enforceLimits(elapsed_time);

        if(isWriteEnabled() && _device->operational())
        {
            //_device->moveJointSpace(_profile_no, joint_position_command_);
            _device->queueJointPosition(_profile_no, joint_position_command_);
        }
    }

    void PreciseHWInterface::enforceLimits(ros::Duration &period)
    {
        pos_jnt_sat_interface_.enforceLimits(period);
    }

    bool PreciseHWInterface::initCb(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
        enableWrite(false);
        if(_device->init(_profile_no, _profile) && _device->home())
        {
            _device->startCommandThread();
            res.success = true;
            _cond_init.notify_one();
        }
        else
            res.success = false;
        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::teachmodeCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        enableWrite(false);

        if(req.data)
        {
            res.success = resetController(false) && _device->freeMode(req.data);
        }
        else
        {
            res.success = resetController(true) && _device->freeMode(req.data);
        }

        enableWrite(true);

        return true;
    }

    bool PreciseHWInterface::homeCb(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
        enableWrite(false);

        if(_device->home())
            res.success = true;
        else
            res.success = false;

        enableWrite(true);

        return true;
    }

    bool PreciseHWInterface::powerCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        enableWrite(false);
        if(_device->setHp(req.data, 5))
            res.success = true;
        else
            res.success = false;

        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::attachCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        enableWrite(false);
        if(_device->attach(req.data))
            res.success = true;
        else
            res.success = false;
        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::cmdCb(cob_srvs::SetString::Request &req, cob_srvs::SetString::Response &res)
    {
        enableWrite(false);
        res.message = _device->command(req.data);
        res.success = true;

        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::graspPlateCB(precise_driver::Plate::Request &req, precise_driver::Plate::Response &res)
    {
        enableWrite(false);
        resetController(false);
        //                                m to mm
        res.success = _device->graspPlate(req.width*1000, req.speed, req.force);
        resetController(true);
        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::releasePlateCB(precise_driver::Plate::Request &req, precise_driver::Plate::Response &res)
    {
        enableWrite(false);
        resetController(false);
        res.success = _device->releasePlate(req.width*1000, req.speed);
        res.success = _device->waitForEom();
        resetController(true);
        enableWrite(true);
        return true;
    }

    bool PreciseHWInterface::gripperCB(precise_driver::Gripper::Request &req, precise_driver::Gripper::Response &res)
    {
        enableWrite(false);
        resetController(false);
        double pos;
        if(req.mode == req.MODE_PERCENT)
        {
            //linear interval transformation
            //f(x) = min + ((max - min)/(b-a)) * (x - a)
            double a, b, min, max;
            a = 0.0; b = 1.0;
            max = joint_position_lower_limits_[4]; min = joint_position_upper_limits_[4];
            pos = min + ((max - min)/(b-a)) * (req.command - a);
        }
        else if(req.mode == req.MODE_POSITION)
        {
            pos = req.command;
        }
        std::vector<double> joints = joint_position_;
        joints[4] = pos;
        bool ret;
        ret = _device->moveJointPosition(_profile_no, joints);
        ret &= _device->waitForEom();
        res.success = ret;

        resetController(true);
        enableWrite(true);
        return true;
    }

    void PreciseHWInterface::enableWrite(bool value)
    {
        std::lock_guard<std::mutex> guard(_mutex_write);
        _write_enabled = value;
    }

    bool PreciseHWInterface::isWriteEnabled()
    {
        bool ret;
        {
            std::lock_guard<std::mutex> guard(_mutex_write);
            ret = _write_enabled;
        }
        return ret;
    }

    bool PreciseHWInterface::resetController(bool active)
    {
        _device->clearCommandQueue();
        controller_manager_msgs::SwitchController::Request req;
        req.strictness = req.BEST_EFFORT;
        if(active)
            req.start_controllers.push_back("joint_trajectory_controller");
        else
            req.stop_controllers.push_back("joint_trajectory_controller");

        controller_manager_msgs::SwitchController::Response res;
        bool ret = _switch_controller_srv.call(req, res);

        if(! (ret && res.ok))
        {
            ROS_ERROR("Can not switch (start/stop) joint_trajectory_controller");
            return false;
        }

        for(size_t i = 0; i < num_joints_; ++i)
        {
            joint_position_command_[i] = joint_position_[i];

            try{
                position_joint_interface_.getHandle(joint_names_[i]).setCommand(joint_position_[i]);
            }
            catch(const hardware_interface::HardwareInterfaceException&)
            {
                ROS_ERROR("can not set command for position_joint_jointerface");
                return false;
            }
        }
        pos_jnt_sat_interface_.reset();
        return (ret && res.ok);
    }

} // namespace precise_driver
