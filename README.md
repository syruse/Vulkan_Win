<!DOCTYPE html>
<html>
<body>

<h1>The game engine powered by Vulkan</h1>
</br> <b>Platforms:</b> Windows/Unix
</br> <b>Technologies:</b> C++ 17, Vulkan Api, SDL2(user input), CMake, WinApi, XCB
<p><img src="demo.png" width="30%" height="30%"></p>
<p>This is simple game engine based on own engine run by Vulkan API, SDL2 (user input)
the developing is ongoing currently it supports such features as
<ol>
  <li>Vulkan pipeline caching to speedup creation of pipelines for the game</li>
  <li>Liquidating tileable pattern for terrain by using multisampling from textures array(2 grass textures and noise texture) with different textcoords and colors mixing
  <p><img src="TerrainImprov.png" width="25%" height="25%"></p></li>
  <li>Skybox(cubic map)</li>
  <li>Shadows(separate depth pass with point of view from light source)</li>
  <li>G-Pass(separate pass with storing depth, normal (using normal mapping) and color to textures and its vulkan subpass for immidiate using produced pixel from main pass to calculate Blinn-Phong lighing, shadow, final color)
  <p><img src="CompositionOfRTT.png" width="45%" height="35%"></p></li>
  <li>FXAA(separate pass for anti aliasing)</li>
  <li>WASD camera manipulation by quaternions</li>
</ol></p>
<p><b>HOWTO BUILD:</b>
</br>
for WIN platform
</br>
set variable SDL pointing to include directory
and ensure that VULKAN_SDK variable is set
</br>
<b><i>cmake.exe ..\Engine\core\ -G "Visual Studio 16 2019"</i></b>
</br>
<b><i>cmake --target "ALL_BUILD" --config "Release"</i></b>
</p>
<p><b>TODO:</b>
</br>
instancing for grass drawing, particles system, fonts, several command buffers, separate thread for resources loading, LODs, oct tree, polishing architecture of the engine</p>
</body>
</html>