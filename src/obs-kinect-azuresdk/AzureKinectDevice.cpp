/******************************************************************************
	Copyright (C) 2020 by Jérôme Leclercq <lynix680@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "AzureKinectDevice.hpp"
#include <util/threading.h>
#include <array>
#include <optional>
#include <tlhelp32.h>

namespace
{
	k4a_device_configuration_t BuildConfiguration(SourceFlags enabledSources, DepthMode depthMode)
	{
		k4a_device_configuration_t deviceConfig;
		deviceConfig.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;
		deviceConfig.subordinate_delay_off_master_usec = 0;
		deviceConfig.disable_streaming_indicator = false;

		if (enabledSources & Source_Color)
		{
			deviceConfig.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
			deviceConfig.color_resolution = K4A_COLOR_RESOLUTION_1080P; //< TODO: Allow to change
		}
		else
		{
			deviceConfig.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
			deviceConfig.color_resolution = K4A_COLOR_RESOLUTION_OFF;
		}

		if (enabledSources & (Source_Depth | Source_ColorToDepthMapping | Source_Infrared))
		{
			switch (depthMode)
			{
				case DepthMode::Passive:
					// Passive IR cannot be used to read depth values, use it when infrared only is requested
					if ((enabledSources & (Source_Depth | Source_ColorToDepthMapping)) == 0)
					{
						deviceConfig.depth_mode = K4A_DEPTH_MODE_PASSIVE_IR;
						break;
					}
					else
						[[fallthrough]];

				case DepthMode::NFOVUnbinned:
					deviceConfig.depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED;
					break;

				case DepthMode::NFOV2x2Binned:
					deviceConfig.depth_mode = K4A_DEPTH_MODE_NFOV_2X2BINNED;
					break;

				case DepthMode::WFOVUnbinned:
					deviceConfig.depth_mode = K4A_DEPTH_MODE_WFOV_UNBINNED;
					break;

				case DepthMode::WFOV2x2Binned:
					deviceConfig.depth_mode = K4A_DEPTH_MODE_WFOV_2X2BINNED;
					break;

				default:
					break;
			}
		}
		else
			deviceConfig.depth_mode = K4A_DEPTH_MODE_OFF;

		if ((enabledSources & Source_Color) && (enabledSources & Source_Depth))
		{
			deviceConfig.synchronized_images_only = true;
			deviceConfig.depth_delay_off_color_usec = -160;
		}
		else
		{
			deviceConfig.synchronized_images_only = false;
			deviceConfig.depth_delay_off_color_usec = 0;
		}
		
		if (deviceConfig.depth_mode == K4A_DEPTH_MODE_WFOV_UNBINNED || deviceConfig.color_resolution == K4A_COLOR_RESOLUTION_3072P)
			deviceConfig.camera_fps = K4A_FRAMES_PER_SECOND_15;
		else
			deviceConfig.camera_fps = K4A_FRAMES_PER_SECOND_30;

		return deviceConfig;
	}

	bool CompareConfig(const k4a_device_configuration_t& lhs, const k4a_device_configuration_t& rhs)
	{
		if (lhs.color_resolution != rhs.color_resolution)
			return false;

		if (lhs.depth_mode != rhs.depth_mode)
			return false;

		if (lhs.camera_fps != rhs.camera_fps)
			return false;

		if (lhs.synchronized_images_only != rhs.synchronized_images_only)
			return false;

		if (lhs.depth_delay_off_color_usec != rhs.depth_delay_off_color_usec)
			return false;

		if (lhs.wired_sync_mode != rhs.wired_sync_mode)
			return false;

		if (lhs.subordinate_delay_off_master_usec != rhs.subordinate_delay_off_master_usec)
			return false;

		if (lhs.disable_streaming_indicator != rhs.disable_streaming_indicator)
			return false;

		return true;
	}
}

AzureKinectDevice::AzureKinectDevice(std::uint32_t deviceIndex) :
m_depthMode(DepthMode::NFOVUnbinned)
{
	m_device = k4a::device::open(deviceIndex);

	SetUniqueName("#" + std::to_string(deviceIndex) + ": " + m_device.get_serialnum());
	SetSupportedSources(Source_Color | Source_Depth | Source_Infrared | Source_ColorToDepthMapping);

	RegisterIntParameter("azuresdk_depth_mode", static_cast<long long>(DepthMode::NFOVUnbinned), [](long long a, long long b)
	{
		return std::max(a, b);
	});
}

AzureKinectDevice::~AzureKinectDevice()
{
	StopCapture(); //< Ensure thread has joined before closing the device
}

obs_properties_t* AzureKinectDevice::CreateProperties() const
{
	obs_property_t* p;

	obs_properties_t* props = obs_properties_create();
	p = obs_properties_add_list(props, "azuresdk_depth_mode", Translate("ObsKinectAzure.DepthMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, Translate("ObsKinectAzure.DepthMode_NFOV_Unbinned"), static_cast<int>(DepthMode::NFOVUnbinned));
	obs_property_list_add_int(p, Translate("ObsKinectAzure.DepthMode_NFOV_2x2Binned"), static_cast<int>(DepthMode::NFOV2x2Binned));
	obs_property_list_add_int(p, Translate("ObsKinectAzure.DepthMode_WFOV_Unbinned"), static_cast<int>(DepthMode::WFOVUnbinned));
	obs_property_list_add_int(p, Translate("ObsKinectAzure.DepthMode_WFOV_2x2Binned"), static_cast<int>(DepthMode::WFOV2x2Binned));
	obs_property_list_add_int(p, Translate("ObsKinectAzure.DepthMode_Passive"), static_cast<int>(DepthMode::Passive));

	return props;
}

void AzureKinectDevice::HandleIntParameterUpdate(const std::string& parameterName, long long value)
{
	if (parameterName == "azuresdk_depth_mode")
	{
		m_depthMode.store(static_cast<DepthMode>(value));
		TriggerSourceFlagsUpdate();
	}
}

void AzureKinectDevice::ThreadFunc(std::condition_variable& cv, std::mutex& m, std::exception_ptr& error)
{
	os_set_thread_name("AzureKinectDevice");

	std::optional<k4a::transformation> transformation;

	k4a_device_configuration_t activeConfig = BuildConfiguration(0, m_depthMode.load());
	SourceFlags enabledSourceFlags = 0;
	bool cameraStarted = false;
	auto UpdateKinectStreams = [&](SourceFlags enabledSources)
	{
		k4a_device_configuration_t newConfig = BuildConfiguration(enabledSources, m_depthMode.load());
		if (!CompareConfig(newConfig, activeConfig))
		{
			// Restart cameras only if configuration changed
			if (cameraStarted)
			{
				m_device.stop_cameras();
				cameraStarted = false;
			}

			m_device.start_cameras(&newConfig);
			cameraStarted = true;
		}

		if (enabledSources & Source_ColorToDepthMapping)
		{
			if (!transformation || activeConfig.depth_mode != newConfig.depth_mode || activeConfig.color_resolution != newConfig.color_resolution)
				transformation = k4a::transformation(m_device.get_calibration(newConfig.depth_mode, newConfig.color_resolution));
		}
		else
			transformation.reset();

		activeConfig = newConfig;
		enabledSourceFlags = enabledSources;
	};

	{
		std::unique_lock<std::mutex> lk(m);
		cv.notify_all();
	} // m & cv no longer exists from here

	if (error)
		return;

	while (IsRunning())
	{
		try
		{
			if (auto sourceFlagUpdate = GetSourceFlagsUpdate())
			{
				try
				{
					UpdateKinectStreams(sourceFlagUpdate.value());
				}
				catch (const std::exception& e)
				{
					errorlog("%s", e.what());

					os_sleep_ms(10);
					continue;
				}
			}

			// Wait until a capture is available
			k4a::capture capture;
			m_device.get_capture(&capture);

			KinectFramePtr framePtr = std::make_shared<KinectFrame>();
			if (enabledSourceFlags & Source_Color)
				framePtr->colorFrame = ToColorFrame(capture.get_color_image());

			if (enabledSourceFlags & (Source_Depth | Source_ColorToDepthMapping))
			{
				k4a::image depthImage = capture.get_depth_image();
				if (enabledSourceFlags & Source_Depth)
					framePtr->depthFrame = ToDepthFrame(depthImage);

				if (enabledSourceFlags & Source_ColorToDepthMapping)
					framePtr->mappedDepthFrame = MapDepthToColorSpace(*transformation, depthImage);
			}

			if (enabledSourceFlags & Source_Infrared)
				framePtr->infraredFrame = ToInfraredFrame(capture.get_ir_image());

			UpdateFrame(std::move(framePtr));
		}
		catch (const std::exception& e)
		{
			errorlog("%s", e.what());

			// Force sleep to prevent log spamming
			os_sleep_ms(100);
		}
	}

	if (cameraStarted)
		m_device.stop_cameras();

	infolog("exiting thread");
}

DepthFrameData AzureKinectDevice::MapDepthToColorSpace(const k4a::transformation& transformation, const k4a::image& depthImage)
{
	k4a::image mappedDepthImage = transformation.depth_image_to_color_camera(depthImage);
	return ToDepthFrame(mappedDepthImage);
}

ColorFrameData AzureKinectDevice::ToColorFrame(const k4a::image& image)
{
	constexpr std::size_t bpp = 4; //< Color is stored as BGRA8

	ColorFrameData colorFrame;
	colorFrame.width = image.get_width_pixels();
	colorFrame.height = image.get_height_pixels();

	std::size_t memSize = colorFrame.width * colorFrame.height * bpp;
	colorFrame.memory.resize(memSize);
	std::uint8_t* memPtr = colorFrame.memory.data();

	colorFrame.ptr.reset(memPtr);
	colorFrame.pitch = colorFrame.width * bpp;
	colorFrame.format = GS_BGRA;

	const uint8_t* imageBuffer = image.get_buffer();
	int imagePitch = image.get_stride_bytes();
	if (colorFrame.pitch == imagePitch)
		std::memcpy(memPtr, imageBuffer, colorFrame.pitch * colorFrame.height);
	else
	{
		std::uint32_t bestPitch = std::min<std::uint32_t>(colorFrame.pitch, imagePitch);
		for (std::size_t y = 0; y < colorFrame.height; ++y)
		{
			const std::uint8_t* input = &imageBuffer[y * imagePitch];
			std::uint8_t* output = memPtr + y * colorFrame.pitch;

			std::memcpy(output, input, bestPitch);
		}
	}

	return colorFrame;
}

DepthFrameData AzureKinectDevice::ToDepthFrame(const k4a::image& image)
{
	constexpr std::size_t bpp = 2; //< Color is stored as R16

	DepthFrameData depthFrame;
	depthFrame.width = image.get_width_pixels();
	depthFrame.height = image.get_height_pixels();

	std::size_t memSize = depthFrame.width * depthFrame.height * bpp;
	depthFrame.memory.resize(memSize);
	std::uint8_t* memPtr = depthFrame.memory.data();

	depthFrame.ptr.reset(reinterpret_cast<std::uint16_t*>(memPtr));
	depthFrame.pitch = depthFrame.width * bpp;

	const uint8_t* imageBuffer = image.get_buffer();
	int imagePitch = image.get_stride_bytes();
	if (depthFrame.pitch == imagePitch)
		std::memcpy(memPtr, imageBuffer, depthFrame.pitch * depthFrame.height);
	else
	{
		std::uint32_t bestPitch = std::min<std::uint32_t>(depthFrame.pitch, imagePitch);
		for (std::size_t y = 0; y < depthFrame.height; ++y)
		{
			const std::uint8_t* input = &imageBuffer[y * imagePitch];
			std::uint8_t* output = memPtr + y * depthFrame.pitch;

			std::memcpy(output, input, bestPitch);
		}
	}

	return depthFrame;
}

InfraredFrameData AzureKinectDevice::ToInfraredFrame(const k4a::image& image)
{
	constexpr std::size_t bpp = 2; //< Color is stored as R16

	InfraredFrameData irFrame;
	irFrame.width = image.get_width_pixels();
	irFrame.height = image.get_height_pixels();

	std::size_t memSize = irFrame.width * irFrame.height * bpp;
	irFrame.memory.resize(memSize);
	std::uint8_t* memPtr = irFrame.memory.data();

	irFrame.ptr.reset(reinterpret_cast<std::uint16_t*>(irFrame.memory.data()));
	irFrame.pitch = irFrame.width * bpp;

	const uint8_t* imageBuffer = image.get_buffer();
	int imagePitch = image.get_stride_bytes();
	if (irFrame.pitch == imagePitch)
		std::memcpy(memPtr, imageBuffer, irFrame.pitch * irFrame.height);
	else
	{
		std::uint32_t bestPitch = std::min<std::uint32_t>(irFrame.pitch, imagePitch);
		for (std::size_t y = 0; y < irFrame.height; ++y)
		{
			const std::uint8_t* input = &imageBuffer[y * imagePitch];
			std::uint8_t* output = memPtr + y * irFrame.pitch;

			std::memcpy(output, input, bestPitch);
		}
	}

	return irFrame;
}