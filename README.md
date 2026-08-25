escalates privileges to nt authority


requires seimpersonateprivilege to be enabled, check with whoami /priv. if no seimpersonateprivilege, you don't have it. you usually unlock it with admin privileges.


build (replace "x86_64-w64-mingw32-gcc" with "gcc" if you're building on windows):
x86_64-w64-mingw32-gcc -O2 -D_M_AMD64 -o privesc.exe privesc.c ms-rprn_c.c -ladvapi32 -lrpcrt4 -static
