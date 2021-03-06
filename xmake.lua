includes(path.join(os.scriptdir(), "config.lua"))

local AzureKinectBodyTrackingSdk = AzureKinectBodyTrackingSdk
local AzureKinectSdk = AzureKinectSdk
local KinectSdk10 = KinectSdk10
local KinectSdk10Toolkit = KinectSdk10Toolkit
local KinectSdk20 = KinectSdk20
local ObsFolder = ObsFolder or {}

rule("kinect_dynlib")
	after_load(function (target)
		target:add("rpathdirs", "@executable_path")
	end)

	on_load("windows", function (target)
		target:set("filename", target:name() .. ".dll")
	end)

	on_load("linux", function (target)
		target:set("filename", target:name() .. ".so")
	end)

	on_load("macosx", function (target)
		target:set("filename", target:name() .. ".dylib")
	end)
rule_end()

rule("copy_to_obs")
	after_build(function(target)
		local folderKey = (is_mode("debug") and "Debug" or "Release") .. (is_arch("x86") and "32" or "64")
		local obsDir = ObsFolder[folderKey]
		if not obsDir then 
			return
		end

		local outputFolder
		if target:name() == "obs-kinectcore" then 
			outputFolder = "bin"
		else
			outputFolder = "obs-plugins"
		end

		local archDir
		if target:is_arch("x86_64", "x64") then
			archDir = "64bit"
		else
			archDir = "32bit"
		end

   		local outputdir = path.join(obsDir, outputFolder, archDir)
		if dir and os.isdir(dir) then
			for _, path in ipairs({ target:targetfile(), target:symbolfile() }) do
				if os.isfile(path) then
					os.vcp(path, dir)
				end
			end
		end
	end)
rule_end()

local function packageGen(outputFolder)
	return function (target)
		import("core.base.option")

		local archDir
		if target:is_arch("x86_64", "x64") then
			archDir = "64bit"
		else
			archDir = "32bit"
		end

   		local outputdir = path.join(option.get("outputdir") or config.buildir(), outputFolder, archDir)
		for _, filepath in ipairs({ target:targetfile(), target:symbolfile() }) do
			if os.isfile(filepath) then
				os.vcp(filepath, path.join(outputdir, path.filename(filepath)))
			end
		end
	end
end

rule("package_bin")
	after_package(packageGen("bin"))
rule_end()

rule("package_plugin")
	after_package(packageGen("obs-plugins"))
rule_end()

add_repositories("local-repo xmake-repo")
add_requires("libfreenect2", { configs = { debug = is_mode("debug") } })

if not AzureKinectSdk then
	add_requires("k4a")
end 

add_requireconfs("libfreenect2", "libfreenect2.libusb", { configs = { pic = true }})

set_project("obs-kinect")
set_version("1.0")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_includedirs("include")
set_languages("c89", "cxx17")
set_license("GPL-3.0")
set_runtimes(is_mode("releasedbg") and "MD" or "MDd")
set_symbols("debug", "hidden")
set_targetdir("./bin/$(os)_$(arch)_$(mode)")
set_warnings("allextra")

add_sysincludedirs(LibObs.Include)

local baseObsDir = path.translate(is_arch("x86") and LibObs.Lib32 or LibObs.Lib64)
if is_plat("windows") then
	local dirSuffix = is_mode("debug") and "Debug" or "Release"
	add_linkdirs(path.join(baseObsDir, dirSuffix))
else
	add_linkdirs(baseObsDir)
end

add_links("obs")

if is_plat("windows") then
	add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
	add_cxxflags("/Zc:__cplusplus", "/Zc:referenceBinding", "/Zc:throwingNew")
	add_cxflags("/w44062") -- Enable warning: Switch case not handled warning
	add_cxflags("/wd4251") -- Disable warning: class needs to have dll-interface to be used by clients of class blah blah blah
elseif is_plat("linux") then
	add_syslinks("pthread")
end

-- Override default package function
on_package(function() end)

target("obs-kinectcore")
	set_kind("shared")
	set_group("Core")

	add_defines("OBS_KINECT_CORE_EXPORT")

	add_headerfiles("include/obs-kinect-core/**.hpp", "include/obs-kinect-core/**.inl")
	add_headerfiles("src/obs-kinect-core/**.hpp", "src/obs-kinect-core/**.inl")
	add_files("src/obs-kinect-core/**.cpp")

	add_includedirs("src")

	add_rules("copy_to_obs", "package_plugin")

target("obs-kinect")
	set_kind("shared")
	set_group("Core")

	add_deps("obs-kinectcore")

	add_headerfiles("src/obs-kinect/**.hpp", "src/obs-kinect/**.inl")
	add_files("src/obs-kinect/**.cpp")

	add_includedirs("src")

	add_rules("kinect_dynlib", "copy_to_obs", "package_plugin")

	on_package(function (target)
		import("core.base.option")
   		local outputdir = option.get("outputdir") or config.buildir()

		os.vcp("data", path.join(outputdir, "data"))
	end)

target("obs-kinect-azuresdk")
	set_kind("shared")
	set_group("Azure")

	add_deps("obs-kinectcore")

	if AzureKinectSdk then
		add_sysincludedirs(AzureKinectSdk.Include)
		add_linkdirs(path.translate(is_arch("x86") and AzureKinectSdk.Lib32 or AzureKinectSdk.Lib64))
		add_links("k4a")
	else
		add_packages("k4a")
	end

	add_headerfiles("src/obs-kinect-azuresdk/**.hpp", "src/obs-kinect-azuresdk/**.inl")
	add_files("src/obs-kinect-azuresdk/**.cpp")

	add_rules("kinect_dynlib", "copy_to_obs", "package_bin")

if KinectSdk10 then
	target("obs-kinect-sdk10")
		set_kind("shared")
		set_group("KinectV1")

		add_defines("UNICODE")
		add_deps("obs-kinectcore")

		add_sysincludedirs(KinectSdk10.Include)
		add_linkdirs(path.translate(is_arch("x86") and KinectSdk10.Lib32 or KinectSdk10.Lib64))
		add_links("Kinect10")

		if KinectSdk10Toolkit then
			add_sysincludedirs(KinectSdk10Toolkit.Include)
		end

		add_headerfiles("src/obs-kinect-sdk10/**.hpp", "src/obs-kinect-sdk10/**.inl")
		add_files("src/obs-kinect-sdk10/**.cpp")

		add_rules("kinect_dynlib", "copy_to_obs", "package_bin")

		if KinectSdk10Toolkit then
			on_package(function (target)
				import("core.base.option")
				local outputdir = option.get("outputdir") or config.buildir()

				local archDir
				local archSuffix
				if target:is_arch("x86_64", "x64") then
					archDir = "64bit"
					archSuffix = "64"
				else
					archDir = "32bit"
					archSuffix = "32"
				end

				local filepath = KinectSdk10Toolkit.Bin .. "/KinectBackgroundRemoval180_" .. archSuffix .. ".dll"
				os.vcp(filepath, path.join(outputdir, "bin", archDir, path.filename(filepath)))
			end)
		end
end

if KinectSdk20 then
	target("obs-kinect-sdk20")
		set_kind("shared")
		set_group("KinectV2")

		add_defines("UNICODE")
		add_deps("obs-kinectcore")

		add_sysincludedirs(KinectSdk10.Include)
		add_linkdirs(path.translate(is_arch("x86") and KinectSdk10.Lib32 or KinectSdk10.Lib64))
		add_links("Kinect20")
		add_syslinks("Advapi32")

		add_headerfiles("src/obs-kinect-sdk20/**.hpp", "src/obs-kinect-sdk20/**.inl")
		add_files("src/obs-kinect-sdk20/**.cpp")

		add_sysincludedirs(path.relative(KinectSdk20.Include, "."))
		add_linkdirs(path.translate(is_arch("x86") and KinectSdk20.Lib32 or KinectSdk20.Lib64))

		add_rules("kinect_dynlib", "copy_to_obs", "package_bin")

		if not is_arch("x86") and os.isdir("thirdparty/NuiSensorLib") then
			add_sysincludedirs("thirdparty/NuiSensorLib/include")
			add_linkdirs("thirdparty/NuiSensorLib/lib/x64")
			add_links("NuiSensorLib")
			add_syslinks("SetupAPI")
		end
end

target("obs-kinect-freenect2")
	set_kind("shared")
	set_group("KinectV2")

	add_deps("obs-kinectcore")
	add_packages("libfreenect2")

	add_headerfiles("src/obs-kinect-freenect2/**.hpp", "src/obs-kinect-freenect2/**.inl")
	add_files("src/obs-kinect-freenect2/**.cpp")

	add_rules("kinect_dynlib", "copy_to_obs", "package_bin")
