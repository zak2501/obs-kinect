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

#pragma once

#ifndef OBS_KINECT_PLUGIN_KINECTDEVICESDK20
#define OBS_KINECT_PLUGIN_KINECTDEVICESDK20

#include "Freenect2Helper.hpp"
#include <obs-kinect/KinectDevice.hpp>

namespace libfreenect2
{
	class Freenect2Device;
	class Frame;
}

class KinectFreenect2Device final : public KinectDevice
{
	public:
		KinectFreenect2Device(libfreenect2::Freenect2Device* device);
		~KinectFreenect2Device();

	private:
		void ThreadFunc(std::condition_variable& cv, std::mutex& m, std::exception_ptr& exceptionPtr) override;

		static ColorFrameData RetrieveColorFrame(const libfreenect2::Frame* frame);
		static DepthFrameData RetrieveDepthFrame(const libfreenect2::Frame* frame);
		static InfraredFrameData RetrieveInfraredFrame(const libfreenect2::Frame* frame);

		libfreenect2::Freenect2Device* m_device;
};

#endif
