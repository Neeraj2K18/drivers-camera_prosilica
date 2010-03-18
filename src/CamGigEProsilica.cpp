/* 
 * File:   GigEProsilica.cpp
 * Author: developer
 * 
 * Created on February 9, 2010, 8:39 AM
 */

#include "CamGigEProsilica.h"
#include <sstream>
#include "CamGigEProsilicaLookUp.h"

//maximal number of cameras connected to the pc
//increase the value if you have more cameras connected
const int kMaxNumberOfCameras = 16;
const int kFrameBufferCount = 3;        //number of buffered frames

//min GigE API requirements 
//this wrapper is not tested with versions below this 
const unsigned long kMinMinorVersion = 20;
const unsigned long kMinMajorVersion = 1;

namespace camera
{
    int CamGigEProsilica::instance_count_ = 0;

    CamGigEProsilica::CamGigEProsilica()
    {
        //versions check
        unsigned long major;
        unsigned long minor;
        PvVersion(&major,&minor);
        if(major < kMinMajorVersion || 
          (major == kMinMajorVersion && minor < kMinMinorVersion))
             throw std::runtime_error("Can not initialize CamGigEProsilica!"
                                      "\nThe GigE driver is too old!");
        
        //initialize api
        camera_handle_ = NULL;
        ++instance_count_;
	
        if(instance_count_ == 1)
        {
            tPvErr err = PvInitialize();
            if(err != ePvErrSuccess)
               throw std::runtime_error("Can not initialize GigEProsilica API!"
                                        "\nSome required system resources "
                                        "are not available.");
            sleep(1);   // otherwise a segmentation fault can occure
                        // if someone uses api calls too early
        }
    }

    CamGigEProsilica::~CamGigEProsilica()
    {
       if(isOpen())
            close();
        
        --instance_count_;
        if(instance_count_ == 0)    //last object deinitializes the
        {                           //api
	    PvUnInitialize();
        }
    }

    int CamGigEProsilica::listCameras(std::vector<CamInfo>&cam_infos)const
    {
        tPvCameraInfo     list[kMaxNumberOfCameras];
        unsigned long     num_cameras,num_cameras2;
        CamInfo          temp_cam_info;

        if(PvCameraCount() == 0)
            return 0;
        
        //get reachable cameras
        temp_cam_info.reachable = true;
        num_cameras = PvCameraList(list, kMaxNumberOfCameras, NULL);
         
        //copy to interface structure
        for (unsigned long i = 0; i < num_cameras; i++)
        {
            copy_tPvCameraInfo_To_tCamInfo(list[i],temp_cam_info);
            fillCameraIpSettings(temp_cam_info);
            cam_infos.push_back(temp_cam_info);
        }
       
        //get unreachable cameras
        temp_cam_info.reachable = false;
        num_cameras2 = PvCameraListUnreachable(list, kMaxNumberOfCameras, NULL);
        //copy to interface structure
        for (unsigned long i = 0; i < num_cameras2; i++)
        {
            copy_tPvCameraInfo_To_tCamInfo(list[i],temp_cam_info);
            fillCameraIpSettings(temp_cam_info);
            cam_infos.push_back(temp_cam_info);
        }
        return ((int)num_cameras+num_cameras2);
    }
    
    
    bool CamGigEProsilica::open(const CamInfo &cam,const AccessMode mode)
    {
        if(isOpen())
            close();

        cam_info_ = cam;
        access_mode_ = mode;
        
        act_grab_mode_= Stop;
        tPvAccessFlags accessflag;
        switch(access_mode_)
        {
            case Monitor:
                accessflag = ePvAccessMonitor;
                break;
            case Master:
            case MasterMulticast:
                accessflag = ePvAccessMaster;
                break;
            default:
                throw std::runtime_error("GigEProsilica does not understand "
                                         "the given access mode!");
        }

        tPvErr result = PvCameraOpen(cam.unique_id,accessflag,&camera_handle_);
        switch(result)
        {
            case ePvErrAccessDenied:
                throw std::runtime_error("Camera could not be opened in the "
                        "requested access mode, because another application is"
                        " using the camera!");
                break;
            case ePvErrNotFound:
                throw std::runtime_error("Camera could not be opened, "
                        "because it can not be found. Maybe it was unplugged.");
                break;
	    case ePvErrInternalFault:
		 throw std::runtime_error("Camera could not be opened. "
                        "Unexpected fault in PvApi or driver. Are you trying to open "
			"the same camera from one process twice?");
	    case ePvErrSuccess:
	   	break;
	    default:
		throw std::runtime_error("Camera could not be opened. Unexpected error.");
        }
        
        //do not change attributes if in monitor mode
        if(access_mode_ != Monitor)
        {
            //can not be set if capturing is started
            //sets the package size to maximum possible
            int iresult = PvCaptureAdjustPacketSize(camera_handle_,16110);
            //set multicast
            if(access_mode_ == MasterMulticast)
                iresult += PvAttrEnumSet(camera_handle_,"MulticastEnable","On");
            else
                iresult += PvAttrEnumSet(camera_handle_,"MulticastEnable","Off");

            if(iresult != 0)
                throw std::runtime_error("Could not set initial camera settings.");
        }
	
	//start capturing
	result = PvCaptureStart(camera_handle_);
	switch(result)
	{
	    case ePvErrUnplugged:
		  throw std::runtime_error("Can not start the image capture "
					    "stream. Camrea was unplugged!");
		  break;
	    case ePvErrResources:
		  throw std::runtime_error("Can not start the image capture "
					    "stream. Required system resources"
					    " were not available.");
	    case ePvErrBandwidth:
		  throw std::runtime_error("Can not start the image capture "
					    "stream. Insufficient bandwidth.");
		  break;
	    case ePvErrAccessDenied:
		    throw std::runtime_error("Can not start the image capture "
					    "stream. Access denied.");
		  break;
	    case ePvErrSuccess:
		  break;
	    case ePvErrBadHandle:
		  throw std::runtime_error("Bad camera handle. Please contact the developer team.");
	    default:
		  throw std::runtime_error("Can not start the image capture "
					    "stream.");
	}
        //reads the actual frame settings of the camera
        getFrameSettings(image_size_,image_mode_,image_color_depth_);
        frame_size_in_byte_ = image_size_.width *
                              image_size_.height *
                              image_color_depth_;
        //store at least one frame in frame_queue_
        ProFrame* frame = new ProFrame(frame_size_in_byte_);
        frame_queue_.push_back(frame);
        return true;
    }
    
    bool CamGigEProsilica::isOpen()const
    {
        if(camera_handle_)
            return true;
        return false;
    }

    bool CamGigEProsilica::grab(const GrabMode mode,const int buffer_len)
    {
        //check if someone tries to change the grab mode
        //during grabbing
        if(act_grab_mode_!= Stop && mode != Stop)
        {
            if(act_grab_mode_ != mode)
                 throw std::runtime_error("Stop grabbing before switching the"
                                          " grab mode!");
            else
                return true;
        }
	
	int result = 0;
        switch(mode)
        {
            case Stop:
            {
                if(access_mode_ != Monitor)
		  PvCommandRun(camera_handle_,"AcquisitionStop");
 		act_grab_mode_ = mode;
 		//all queued frames have to be cleared
                result += PvCaptureQueueClear(camera_handle_);	
	        break;
            }
            case SingleFrame:
                prepareQueueForGrabbing(1);
		if(access_mode_ != Monitor)
		{
		  result = PvAttrEnumSet(camera_handle_,"AcquisitionMode","SingleFrame");
		  result += PvCommandRun(camera_handle_,"AcquisitionStart");
		}
		act_grab_mode_ = mode;
                break;

            case MultiFrame:
	        act_grab_mode_ = mode;
                break;

            case Continuously:
                prepareQueueForGrabbing(buffer_len);
		if(access_mode_ != Monitor)
		{
		  result = PvAttrEnumSet(camera_handle_,"AcquisitionMode","Continuous");
		  result += PvCommandRun(camera_handle_,"AcquisitionStart");
		  
		}
		act_grab_mode_ = mode;
		break;
            default:
                throw std::runtime_error("The grab mode is not supported by"
                                         " the camera!");
        }
        if(result != 0)
	    throw std::runtime_error("Can not start/stop grabbing!");
        return true;
    }

    bool CamGigEProsilica::isFrameAvailable()
    {
        if(act_grab_mode_!=Stop && !isFrameQueued(frame_queue_.front()))
            return true;
        return false;
    }

    int CamGigEProsilica::skipFrames()
    {
        //keep only the newest frame
        //requeue all other frames
        ProFrame *frame = NULL;

        int i;
        int isize = frame_queue_.size()-1;
        for(i = 0; i < isize &&
            !isFrameQueued(*(frame_queue_.begin()++));++i)
        {
            frame = frame_queue_.front();
            frame_queue_.pop_front();
            frame_queue_.push_back(frame);
            queueFrame(frame);
        }
        if(i == isize)
            throw std::runtime_error("Frames lost! Call retrieve more often"
                        " or reduse fps!");
        return i;
    }

    bool CamGigEProsilica::retrieveFrame(Frame &frame,const int timeout)
    {
        tPvErr result;
        ProFrame *pframe = frame_queue_.front();
        
        //waiting for specific frame
        for(int i =0;isFrameQueued(pframe); ++i)
        {
            if( i<timeout)
                usleep(1000);   //wait 1ms
            else
                return false;
        }

        //can not use api function. During heavy cpu load callback fcn is
        //called to late
        //result = PvCaptureWaitForFrameDone
          //                        (camera_handle_,&pframe->frame,timeout);

        //swap buffers
        pframe->swap(frame);
        frame.attributes.clear();
	if(pframe->frame.Status == ePvErrSuccess) 
	  frame.setStatus(STATUS_VALID);
	else
	  frame.setStatus(STATUS_INVALID);
   
        // there is no way to check by the api
        // if Acquistion hast stopped automatically
        switch(act_grab_mode_)
        {
            case Stop:
                 throw std::runtime_error("Call grab before retrieve!");

            case SingleFrame:
                 act_grab_mode_ = Stop;
                 break;

            case Continuously:
            {
                //save queue status
                bool queue_empty = !isFrameQueued(frame_queue_.back());

                //move frame to the back
                frame_queue_.pop_front();
                frame_queue_.push_back(pframe);

                 //requeue frame
                queueFrame(pframe);

                //check if no frames are lost
                if(queue_empty)
                   throw std::runtime_error("Frames lost! Call retrieve"
                        " more often or reduse fps!");
            }
        }
        return true;
    }

    bool CamGigEProsilica::close()
    {
        if(isOpen())
        {
            int result = 0;
	    if(act_grab_mode_ != Stop && access_mode_ != Monitor)
	    {
	      result = PvCommandRun(camera_handle_,"AcquisitionAbort");
	    }
	   
	    if(result)
	      throw std::runtime_error("error can not delete queue");
           
            act_grab_mode_ = Stop;
            PvCaptureQueueClear(camera_handle_);
            PvCaptureEnd(camera_handle_);
            PvCameraClose(camera_handle_);	
	    
             //delete queue
            while(!frame_queue_.empty())
            {
                ProFrame *frame = frame_queue_.back();
		if(!isFrameQueued(frame))
		{
		  delete frame;
		  frame_queue_.pop_back();
		}
		else
		{
		  throw std::runtime_error("error can not delete queue");
		}
            }     
            camera_handle_= NULL;
        }
        return true;
    }

    //helper
    void CamGigEProsilica::copy_tPvCameraInfo_To_tCamInfo
                             (const tPvCameraInfo& source, CamInfo& dest)const
    {
        dest.display_name = source.DisplayName;
        dest.interface_id = source.InterfaceId;
        switch(source.InterfaceType)
        {
            case ePvInterfaceFirewire:
                dest.interface_type = InterfaceFirewire;
                break;
            case ePvInterfaceEthernet:
                dest.interface_type = InterfaceEthernet;
                break;
            default:
                dest.interface_type = InterfaceUnknown;
        }
        dest.part_number = source.PartNumber;
        dest.part_version = source.PartVersion;
        dest.permitted_access = source.PermittedAccess;
        dest.serial_string = source.SerialString;
        dest.unique_id = source.UniqueId;
    }

    //helper
    //fills out the ip settings for a specific camera
    bool CamGigEProsilica::fillCameraIpSettings(CamInfo& cam)const
    {
        tPvIpSettings ip_settings;
        tPvErr result = PvCameraIpSettingsGet(cam.unique_id,&ip_settings);

        if(result == ePvErrNotFound)    //can not reach camera
              return false;             //this is none critical

       //copy structur tPvIpSettings to tIPSettings
       cam.ip_settings.config_mode_support = ip_settings.ConfigModeSupport;
       cam.ip_settings.current_ip_address = ip_settings.CurrentIpAddress;
       cam.ip_settings.current_ip_gateway = ip_settings.CurrentIpGateway;
       cam.ip_settings.current_ip_subnet = ip_settings.CurrentIpSubnet;
       cam.ip_settings.persisten_ip_addr = ip_settings.PersistentIpAddr;
       cam.ip_settings.persistent_ip_gateway = ip_settings.PersistentIpGateway;
       cam.ip_settings.persistent_ip_subnet = ip_settings.PersistentIpSubnet;

       switch (ip_settings.ConfigMode)
       {
           case ePvIpConfigPersistent:
                cam.ip_settings.config_mode = IpConfigPersistent;
               break;
           case ePvIpConfigDhcp:
                cam.ip_settings.config_mode = IpConfigDhcp;
               break;
           case ePvIpConfigAutoIp:
                cam.ip_settings.config_mode = IpConfigAutoIp;
               break;
           default:
                cam.ip_settings.config_mode = IpConfigUnknown;
       }
       return true;
    }

    bool CamGigEProsilica::prepareQueueForGrabbing(const int queue_len)
    {
        //remove frames if queue is too long 
        if(queue_len < frame_queue_.size())
        {
             if( act_grab_mode_ != Stop)
                throw std::runtime_error("Can not reduce frame queue during "
                                         "grabbing!!!");
             
             if(queue_len == 0)
                 throw std::runtime_error("It is not allowed to reduce the "
                                          "frame queue to zero!");
             
             //all queued frames have to be cleared
             PvCaptureQueueClear(camera_handle_);   
            
             //remove frames
             while(frame_queue_.size() > queue_len)
             {
                 if(isFrameQueued(frame_queue_.back()))
                     throw std::runtime_error("Queue can not be reduced."
                                               " Frames are still used.");
                 ProFrame *frame = frame_queue_.front();
                 delete frame;
                 frame_queue_.pop_back();
             }
        }

        //requeue all other frames
         std::list<ProFrame *>::iterator _iter = frame_queue_.begin();
         for(;_iter!= frame_queue_.end(); _iter++)
              queueFrame(*_iter);

        //add frames if the queue is too short
        int iend = queue_len-frame_queue_.size();
        for(int i=0;i<iend;i++)
        {
           ProFrame *frame = new ProFrame(frame_size_in_byte_);
           frame_queue_.push_back(frame);
           queueFrame(frame);
        }
    }
    
    bool CamGigEProsilica::setFrameSettings(const frame_size_t size,
                                            const frame_mode_t mode,
                                            const uint8_t color_depth,
                                            const bool resize_frames)
    {
        if(act_grab_mode_ != Stop)
        {
            throw std::runtime_error("You can not change the frame size "
                                     "during grabbing!");
        }

        int size_in_byte = size.width*size.height*color_depth;
        std::string pixel_format = convertPixelFormatToStr(mode, color_depth);
        
        int result = 0;
        //all queued frames have to be cleared
        result=PvCaptureQueueClear(camera_handle_);

         //set new camera settings
        result+=PvAttrEnumSet(camera_handle_,"PixelFormat",pixel_format.c_str());
        result+=PvAttrUint32Set(camera_handle_,"Width",size.width);
        result+=PvAttrUint32Set(camera_handle_,"Height",size.height);

        if(result != 0)
	{
	  std::stringstream ss1;
	  std::stringstream ss2;
	  ss1 << size.width;
	  ss2 << size.height;
	  throw std::runtime_error("Can not set camera attribute width="+ss1.str()+
				   ", height="+ss2.str()+ " or format=" + pixel_format);
	}
        //resize buffered frames if frames have the wrong size
        if(resize_frames)
        {
            image_size_ = size;
            image_mode_ = mode;
            image_color_depth_ = color_depth;
            frame_size_in_byte_ = size_in_byte;

            std::list<ProFrame *>::iterator _iter = frame_queue_.begin();
            for(;_iter!=frame_queue_.end(); _iter++)
            {
                if((*_iter)->frame.ImageBufferSize!= frame_size_in_byte_)
                {
                    if(isFrameQueued(*_iter))
                         throw std::runtime_error("Can not resize frame."
                                                  " Frame is still used.");
                    (*_iter)->resize(frame_size_in_byte_);
                }
            }
            
            //check if bufferd frames and camerea frames have the same size
            tPvUint32 frame_size = 0;
            PvAttrUint32Get(camera_handle_,"TotalBytesPerFrame",&frame_size);
            if(frame_size != frame_size_in_byte_)
            {
                //critical error close camera
                 std::cout << (int)color_depth;
                std::cout << frame_size << "  " << frame_size_in_byte_;
                close();
                throw std::runtime_error("Error: Calc frame size differs from "
                                         "real frame size!!!");
            }
        }
        return true;
    }

    inline bool CamGigEProsilica::isFrameQueued(const ProFrame * frame)
    {
        if(frame->frame.AncillaryBufferSize == 1)
            return true;
        return false;
    }

    inline bool CamGigEProsilica::queueFrame(ProFrame *frame)
    {
       if(isFrameQueued(frame))
            throw std::runtime_error("Cannot queue frame! Frame is allready"
                    " queued.");

        //using AncillaryBufferSize to indicate if frame is done
        //0 --> frame is done
        //1 --> frame is not done
        //faster than PvCaptureWaitForFrameDone
        frame->frame.AncillaryBufferSize = 1;
        tPvErr result = PvCaptureQueueFrame(camera_handle_,
                                            &frame->frame,callBack);
        if(result!= ePvErrSuccess)
        {
            frame->frame.AncillaryBufferSize = 0;
            if(access_mode_ == Monitor)
                throw std::runtime_error("Cannot queue frame! "
                        "Maybe Master has not started capturing.");
            else
                throw std::runtime_error("Cannot queue frame!");
        }
        return true;
    }

    //called by the api if frame is done
    void CamGigEProsilica::callBack(tPvFrame * frame)
    {
       //using AncillaryBufferSize to indicate if frame is done
       //0 --> frame is done
       //1 --> frame is not done
       //faster than PvCaptureWaitForFrameDone
        frame->AncillaryBufferSize = 0;     //indicates that frame is done
    }

    bool CamGigEProsilica::setAttrib(const int_attrib::CamAttrib attrib,
                                     const int value)
    {
        if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        tPvErr result;
        std::string indent;
        attribToStr(attrib, indent);
        result = PvAttrUint32Set( camera_handle_, indent.c_str(), value);
	if (result != ePvErrSuccess)
	{
           std::stringstream ss;  
	   ss << value;
	   throw std::runtime_error("Can not set attribute "+ indent +
					      " to " +ss.str());
	}
        return true;
    }

     bool CamGigEProsilica::setAttrib(const double_attrib::CamAttrib attrib,
                                     const double value)
    {
        if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        tPvErr result;
        std::string indent;
        attribToStr(attrib, indent);
        result = PvAttrFloat32Set( camera_handle_,indent.c_str(),
                                  (tPvFloat32)value);
        if (result != ePvErrSuccess)
	{
           std::stringstream ss;  
	   ss << value;
	   throw std::runtime_error("Can not set attribute "+ indent +
					      " to " +ss.str());
	}
        return true;
    }

    bool CamGigEProsilica::setAttrib(const enum_attrib::CamAttrib attrib)
    {
        if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        tPvErr result;
        std::string indent;
        std::string value;
        attribToStr(attrib, indent,value);
        result = PvAttrEnumSet (camera_handle_, indent.c_str(), value.c_str());
        if (result != ePvErrSuccess)
            throw std::runtime_error("Can not set attribute " + indent + " to " + value);
        return true;
    }

    bool CamGigEProsilica::setAttrib(const str_attrib::CamAttrib attrib,
                                     const std::string &string)
    {
        if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        tPvErr result;
        std::string indent;
        attribToStr(attrib, indent);
        result = PvAttrStringSet(camera_handle_, indent.c_str(), string.c_str());
        if (result != ePvErrSuccess)
            throw std::runtime_error("Can not set attribute "  + 
						      indent + " to " + string);
        return true;
    }

     bool CamGigEProsilica::isAttribAvail(const int_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         try
         {
            attribToStr(attrib, indent);
         }
         catch(std::runtime_error e)
         {
                 return false;
         }
         tPvErr result = PvAttrIsAvailable(camera_handle_,indent.c_str());
         if(result == ePvErrSuccess)
             return true;
         return false;
     }

     bool CamGigEProsilica::isAttribAvail(const double_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         try
         {
            attribToStr(attrib, indent);
         }
         catch(std::runtime_error e)
         {
                 return false;
         }
         tPvErr result = PvAttrIsAvailable(camera_handle_,indent.c_str());
         if(result == ePvErrSuccess)
             return true;
         return false;
     }

     bool CamGigEProsilica::isAttribAvail(const str_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         try
         {
            attribToStr(attrib, indent);
         }
         catch(std::runtime_error e)
         {
                 return false;
         }
         tPvErr result = PvAttrIsAvailable(camera_handle_,indent.c_str());
         if(result == ePvErrSuccess)
             return true;
         return false;
     }

     bool CamGigEProsilica::isAttribAvail(const enum_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent,value;
         try
         {
            attribToStr(attrib, indent,value);
         }
         catch(std::runtime_error e)
         {
                 return false;
         }
         tPvErr result = PvAttrIsAvailable(camera_handle_,indent.c_str());
         if(result == ePvErrSuccess)
             return true;
         return false;
     }
     
     int CamGigEProsilica::getAttrib(const int_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         attribToStr(attrib, indent);
         tPvUint32 value;
         tPvErr result = PvAttrUint32Get(camera_handle_,indent.c_str(),&value);
         if(result != ePvErrSuccess)
              throw std::runtime_error("Can not get attribute " +  indent);
         return value;
     }

     double CamGigEProsilica::getAttrib(const double_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         attribToStr(attrib, indent);
         tPvFloat32 value;
         tPvErr result = PvAttrFloat32Get(camera_handle_,indent.c_str(),&value);
         if(result != ePvErrSuccess)
              throw std::runtime_error("Can not get attribute " +  indent);
         return value;
     }

     std::string CamGigEProsilica::getAttrib(const str_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

         std::string indent;
         attribToStr(attrib, indent);
         char value[64];
         long unsigned int lengh = 0;
         tPvErr result = PvAttrStringGet(camera_handle_,
                                         indent.c_str(),
                                         value,64,&lengh);
         if(result != ePvErrSuccess)
              throw std::runtime_error("Can not get attribute " +  indent);
         return std::string(value,std::min(64,(int)lengh));
     }

     bool CamGigEProsilica::isAttribSet(const enum_attrib::CamAttrib attrib)
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        tPvErr result;
        std::string indent;
        std::string value;
        char buffer[64];
        unsigned long int length;
        attribToStr(attrib, indent,value);
        result = PvAttrEnumGet (camera_handle_, indent.c_str(),
                 buffer,64,&length);
        if (result != ePvErrSuccess)
            throw std::runtime_error("Can not get attribute " +  indent);

        if(std::string(buffer,std::min((int)length,64))==value)
            return true;
        return false;
     }

     bool CamGigEProsilica::triggerFrame()
     {
         if (!camera_handle_)
            throw std::runtime_error("No camera is open!");
         tPvErr result =PvCommandRun(camera_handle_,"FrameStartTriggerSoftware");
         if(result != ePvErrSuccess)
              throw std::runtime_error("Can not trigger frame! Are you in "
                      "Software FrameStartTriggerMode?");
         return true;
     }

     bool CamGigEProsilica::getFrameSettings(frame_size_t &size,
                                             frame_mode_t &mode,
                                             uint8_t &color_depth)
     {
        if (!camera_handle_)
            throw std::runtime_error("No camera is open!");

        char pixel_format[32];
        unsigned long int ilen;
        tPvUint32 width,height;
        int result;

        //get camera settings
        result=PvAttrEnumGet(camera_handle_,"PixelFormat",pixel_format,32,&ilen);
        result+=PvAttrUint32Get(camera_handle_,"Width",&width);
        result+=PvAttrUint32Get(camera_handle_,"Height",&height);
        if(result)
            throw std::runtime_error("Can not read camera attributes width, height and format!");
        size.height = height;
        size.width = width;

        std::string str(pixel_format,std::min(32,(int)ilen));
        convertStrToPixelFormat(str, mode, color_depth);
        return true;
     }
}