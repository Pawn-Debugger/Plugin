[PAWN debugger plugin][github]
============================

[![Version][version_badge]][version]

[Idea discussion, and why built-in debug hooks are not good enough for our case](http://forum.sa-mp.com/showthread.php?t=647654)

This will be a debugger plugin for sa-mp servers, allowing for creation of better AMX runtime debugging tools. Currently it's just a hard fork of Zeex's [crashdetect plugin, as of commit `cdd586d60a55455018fa63f7c7b76cf952349f88`](https://github.com/Zeex/samp-plugin-crashdetect/commit/cdd586d60a55455018fa63f7c7b76cf952349f88). If all goes well, all that should remain of that plugin in this repository is code structure and coding conventions, build configurations, and virtual machine hijacker.

Be aware that at the same time this are my C++ learning grounds, so not everything will be always 100%. Issues are open - all comments are welcome.

Building on Linux
-----------------

Install gcc and g++, make and cmake. On Ubuntu you would do that with:

```
sudo apt-get install gcc g++ make cmake
```

If you're building on a 64-bit system you'll need multilib packages for gcc and g++:

```
sudo apt-get install gcc-multilib g++-multilib
```

If you're building on CentOS, install the following packages:

```
yum install gcc gcc-c++ cmake28 make
```

Now you're ready to build the plugin:

```
cd Plugin
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
make
```

Building on Windows
-------------------

You'll need to install CMake and Visual Studio (Express edition will suffice).
After that, either run cmake from the command line:

```
cd Plugin
mkdir build && cd build
path/to/cmake.exe ../ -DBUILD_TESTING=OFF
```

or do the same from cmake-gui. This will generate a Visual Studio project in
the build folder.

To build the project:

```
path/to/cmake.exe --build . --config Release
```

You can also build it from within Visual Studio: open build/Plugin.sln and
go to menu -> Build -> Build Solution (or just press F7).

License
-------

Licensed under the 2-clause BSD license. See the LICENSE.txt file.

[github]: https://github.com/Pawn-Debugger/Plugin
[version]: http://badge.fury.io/gh/Pawn-Debugger%2FPlugin
[version_badge]: https://badge.fury.io/gh/Pawn-Debugger%2FPlugin.svg
