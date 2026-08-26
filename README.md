это повышает права до уровня nt authority


для этого необходимо, чтобы было включено право "seimpersonateprivilege"; проверьте это с помощью команды "whoami /priv". если право "seimpersonateprivilege" отсутствует, значит, у вас его нет. обычно его включают с помощью прав администратора.


собрать (замените "x86_64-w64-mingw32-gcc" на "gcc", если вы выполняете сборку в операционной системе Windows)
x86_64-w64-mingw32-gcc -O2 -D_M_AMD64 -o privesc.exe privesc.c ms-rprn_c.c -ladvapi32 -lrpcrt4 -static


en

this escalates privileges to nt authority


requires "seimpersonateprivilege" to be enabled, check with "whoami /priv". if no "seimpersonateprivilege", you don't have it. you usually unlock it with admin privileges.


build (replace "x86_64-w64-mingw32-gcc" with "gcc" if you're building on windows operating system):
x86_64-w64-mingw32-gcc -O2 -D_M_AMD64 -o privesc.exe privesc.c ms-rprn_c.c -ladvapi32 -lrpcrt4 -static
