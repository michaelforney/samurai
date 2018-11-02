@SET VC=%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC
@SET KIT81=%ProgramFiles(x86)%\Windows Kits\8.1
@SET KIT10=%ProgramFiles(x86)%\Windows Kits\10

@SET INCLUDE=%VC%\include;%KIT10%\Include\10.0.10240.0\ucrt;%KIT81%\Include\shared;%KIT81%\Include\um;%KIT81%\Include\winrt
@SET LIB=%VC%\lib\amd64;%KIT10%\Lib\10.0.10240.0\ucrt\x64;%KIT81%\lib\winv6.3\um\x64

@SET CFLAGS=/nologo /c /W3 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Od /Z7 /Zo

cl %CFLAGS% build.c
cl %CFLAGS% env.c
cl %CFLAGS% deps.c
cl %CFLAGS% graph.c
cl %CFLAGS% htab.c
cl %CFLAGS% log.c
cl %CFLAGS% parse.c
cl %CFLAGS% platform.c
cl %CFLAGS% samurai.c
cl %CFLAGS% scan.c
cl %CFLAGS% tool.c
cl %CFLAGS% tree.c
cl %CFLAGS% util.c
cl /nologo /Fesamu.exe build.obj env.obj deps.obj graph.obj htab.obj log.obj parse.obj platform.obj samurai.obj scan.obj tool.obj tree.obj util.obj
