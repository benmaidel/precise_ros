#include <precise_driver/precise_hw_interface.h>
#include <angles/angles.h>

namespace precise_driver
{
    PreciseHWInterface::PreciseHWInterface(ros::NodeHandle &nh, urdf::Model *urdf_model)
        : ros_control_boilerplate::GenericHWInterface(nh, urdf_model)
    {
        ros::NodeHandle pnh(nh_, "hardware_interface");
        ros::NodeHandle driver_nh(nh_, "driver");

        std::string ip;
        pnh.param<std::string>("ip_address", ip, ip);
        int port;
        pnh.param<int>("port", port, port);
        pnh.param<int>("profile_no", _profile_no, _profile_no);
        pnh.param<int>("speed", _profile.speed, _profile.speed);
        pnh.param<int>("speed2", _profile.speed2, _profile.speed2);
        pnh.param<int>("accel", _profile.accel, _profile.accel);
        pnh.param<int>("decel", _profile.decel, _profile.decel);
        pnh.param<double>("accel_ramp", _profile.accel_ramp, _profile.accel_ramp);
        pnh.param<double>("decel_ramp", _profile.decel_ramp, _profile.decel_ramp);
        pnh.param<int>("in_range", _profile.in_range, _profile.in_range);
        pnh.param<int>("straight", _profile.straight, _profile.straight);

        ROS_INFO_STREAM("ip: "<<ip);
        ROS_INFO_STREAM("port: "<<port);

        _device.reset(new PFlexDevice(std::make_shared<PreciseTCPInterface>(ip, port)));

        _init_srv = driver_nh.advertiseService("init", &PreciseHWInterface::initCb, this);
        _teachmode_srv = driver_nh.advertiseService("teach_mode", &PreciseHWInterface::teachmodeCb, this);
        _home_srv = driver_nh.advertiseService("home", &PreciseHWInterface::homeCb, this);
        _power_srv = driver_nh.advertiseService("power", &PreciseHWInterface::powerCb, this);
        _cmd_srv = driver_nh.advertiseService("command", &PreciseHWInterface::cmdCb, this);
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
        ROS_INFO("Init Done");

        GenericHWInterface::init();
        std::vector<double> joints = _device->getJointPositions();

        for(size_t i = 0; i < num_joints_; i++)
        {
            joint_position_[i] = joints[i];
        }
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

        if(_device->operable())
        {
            //_device->moveJointSpace(_profile_no, joint_position_command_);
            _device->queueJointSpace(_profile_no, joint_position_command_);
        }
    }

    void PreciseHWInterface::enforceLimits(ros::Duration &period)
    {
        pos_jnt_sat_interface_.enforceLimits(period);
    }

    bool PreciseHWInterface::initCb(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
        if(_device->init(_profile_no, _profile) && _device->home())
        {
            _device->startMoveJThread();
            res.success = true;
            _cond_init.notify_one();
        }
        else
            res.success = false;
        return true;
    }

    bool PreciseHWInterface::teachmodeCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        if(_device->freeMode(req.data))
            res.success = true;
        else
            res.success = false;

        return true;
    }

    bool PreciseHWInterface::homeCb(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
        if(_device->home())
            res.success = true;
        else
            res.success = false;

    }

    bool PreciseHWInterface::powerCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        if(_device->setHp(req.data, 5))
            res.success = true;
        else
            res.success = false;
        return true;
    }

    bool PreciseHWInterface::attachCb(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
    {
        if(_device->attach(req.data))
            res.success = true;
        else
            res.success = false;
        return true;
    }

    bool PreciseHWInterface::cmdCb(cob_srvs::SetString::Request &req, cob_srvs::SetString::Response &res)
    {
        res.message = _device->command(req.data);
        res.success = true;

        return true;
    }

} // namespace precise_driver
