scons platform=linuxbsd target=editor precision=double

python3 misc/scripts/install_d3d12_sdk_windows.py
scons platform=windows target=template_debug arch=x86_64 tools=no precision=double use_mingw=yes

scons platform=linuxbsd target=template_debug arch=x86_64 tools=no precision=double

cd platform/android/java

# ./gradlew clean

cd ../../..

# Compile the Android build templates.
scons platform=android arch=arm32 target=template_debug build_profile="kaetram.build" precision=double 
scons platform=android arch=arm64 target=template_debug build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_32 target=template_debug build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_64 target=template_debug build_profile="kaetram.build" precision=double 

scons platform=android arch=arm32 target=template_release build_profile="kaetram.build" precision=double 
scons platform=android arch=arm64 target=template_release build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_32 target=template_release build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_64 target=template_release build_profile="kaetram.build" precision=double 

# Compile via Gradle.
cd platform/android/java

JAVA_HOME=/root/jdk-21.0.9 PATH=/root/jdk-21.0.9/bin:$PATH ./gradlew generateGodotTemplates

