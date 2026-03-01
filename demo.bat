mkdir build
cd build
cmake .. -DPMM_BUILD_DEMO=ON
cmake --build . --config=Release --target pmm_demo
cd ..
build\demo\Release\pmm_demo.exe
