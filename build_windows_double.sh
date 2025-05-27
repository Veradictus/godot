# Build the editor.
scons platform=windows precision=double

# Build the export templates for Windows.
scons platform=windows target=template_debug arch=x86_64 tools=no precision=double build_profile="kaetram.build" "angle_libs=C:\Users\flavi\Documents\Projects\angle"
scons platform=windows target=template_release arch=x86_64 tools=no precision=double build_profile="kaetram.build" "angle_libs=C:\Users\flavi\Documents\Projects\angle"

# Rename the templates and place them in the correct directory.
cp bin/godot.windows.template_debug.x86_64.console.exe bin/windows_debug_x86_64_console.exe
cp bin/godot.windows.template_debug.x86_64.exe bin/windows_debug_x86_64.exe
cp bin/godot.windows.template_release.x86_64.console.exe bin/windows_release_x86_64_console.exe
cp bin/godot.windows.template_release.x86_64.exe bin/windows_release_x86_64.exe

# Copy the Windows templates to the templates_windows directory.
mkdir -p bin/templates_windows

mv bin/windows_debug_x86_64_console.exe bin/templates_windows/windows_debug_x86_64_console.exe
mv bin/windows_debug_x86_64.exe bin/templates_windows/windows_debug_x86_64.exe
mv bin/windows_release_x86_64_console.exe bin/templates_windows/windows_release_x86_64_console.exe
mv bin/windows_release_x86_64.exe bin/templates_windows/windows_release_x86_64.exe

cd platform/android/java

./gradlew clean

cd ../../..

# Compile the Android build templates.
# scons platform=android arch=arm32 target=template_debug build_profile="kaetram.build" precision=double 
scons platform=android arch=arm64 target=template_debug build_profile="kaetram.build" precision=double 
# scons platform=android arch=x86_32 target=template_debug build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_64 target=template_debug build_profile="kaetram.build" precision=double 

# scons platform=android arch=arm32 target=template_release build_profile="kaetram.build" precision=double 
scons platform=android arch=arm64 target=template_release build_profile="kaetram.build" precision=double 
# scons platform=android arch=x86_32 target=template_release build_profile="kaetram.build" precision=double 
scons platform=android arch=x86_64 target=template_release build_profile="kaetram.build" precision=double 

# Compile via Gradle.
cd platform/android/java

./gradlew generateGodotTemplates