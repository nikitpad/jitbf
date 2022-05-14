build:
	gcc jitbf.c -o jitbf -O3 -eWinMain -ffreestanding -s -nostdlib -lkernel32 -luser32 -lshell32
