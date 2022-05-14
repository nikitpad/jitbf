#include <windows.h>

typedef PBYTE LABEL;

VOID (*outputFunction)(int chr);
PULONGLONG (*inputFunction)(void), memory;
LABEL jitted, jittedStart;
PBYTE code;
HANDLE stdout;
BYTE buffer[8192];
int bufferPos = 0;

#define CALL_OUTPUT
#define OUTPUT8(x)  *jitted++ = (x);
#define OUTPUT16(x) *(PWORD)jitted      = (x), jitted += 2;
#define OUTPUT32(x) *(PDWORD)jitted     = (x), jitted += 4;
#define OUTPUT64(x) *(PULONGLONG)jitted = (x), jitted += 8;

VOID ConsoleOutput(int chr)
{
	DWORD dwWritten;
	buffer[bufferPos++] = chr;
	if (bufferPos == 8192)
	{
		WriteConsoleA(stdout, buffer, 8192, &dwWritten, NULL);
		bufferPos = 0;
	}
}

// Recognize [-@(k)+@(-k)] pattern
INT PatternZeroAdd()
{
	PBYTE pCode = code;
	if (pCode[0] == '[' && pCode[1] == '-' && pCode[2] == '>')
	{
		pCode += 2;
		int n = 0, k = 0;
		for (; *pCode == '>'; n++, *pCode++);
		if (*pCode == '+') *pCode++;
		for (; *pCode == '<'; k++, *pCode++);
		if (n && n == k && *pCode == ']')
			return n;
	}
	return 0;
}

BOOL PatternZero()
{
	if (code[0] == '[' && (code[1] == '-' || code[1] == '+')
		&& code[2] == ']')
		return TRUE;
	return FALSE;
}

VOID Compile(BOOL isLoop, PBYTE loopStartOffset)
{
	if (!isLoop)
	{		
		// push rsi
		// push rdi
		// push rbp
		OUTPUT16(0x5756)
		OUTPUT8(0x55)
		// mov rdx, rcx
		// xor ecx, ecx
		OUTPUT8(0x48)
		OUTPUT8(0x89)
		OUTPUT8(0xCA)
		OUTPUT8(0x31)
		OUTPUT8(0xC9)
	}
	
	char acount = 0;
	int mcount = 0;
	char c;
	for (;;)
	{
		switch (c = *code)
		{
			case '+': case '-':
				for (; *code == '+' || *code == '-'; code++)
					if (*code == '-') acount--;
					else acount++;
				if (acount == 1)
				{
					OUTPUT8(0xFE) OUTPUT8(0xC1)
				}
				else if (acount == -1)
				{
					OUTPUT8(0xFE) OUTPUT8(0xC9)
				}
				else
				{
					OUTPUT8(0x80)
					OUTPUT8(acount < 0 ? 0xE9 : 0xC1)
					OUTPUT8(acount < 0 ? -acount : acount)
				}
				acount = 0;
				break;
			case '>': case '<':
				for (; *code == '>' || *code == '<'; code++)
					if (*code == '<') mcount--;
					else mcount++;
					
				OUTPUT8(0x88) OUTPUT8(0x0A)
				
				if (mcount == 1)
				{
					OUTPUT8(0x48) OUTPUT8(0xFF) OUTPUT8(0xC2)
				}
				else if (mcount == -1)
				{
					OUTPUT8(0x48) OUTPUT8(0xFF) OUTPUT8(0xCA)
				}
				else
				{
					OUTPUT8(0x48)
					OUTPUT8(0x81)
					OUTPUT8(mcount < 0 ? 0xEA : 0xC2);
					OUTPUT32(mcount < 0 ? -mcount : mcount);
				}
				
				OUTPUT8(0x8A) OUTPUT8(0x0A)
				mcount = 0;
				break;
			case '.':
				#ifdef CALL_OUTPUT
				// mov rsi, rcx
				// mov rdi, rdx
				// call qword [???]
				// mov rcx, rsi
				// mov rdx, rdi
				OUTPUT8(0x48) OUTPUT8(0x89) OUTPUT8(0xCE) OUTPUT8(0x48)
				OUTPUT8(0x89) OUTPUT8(0xD7) OUTPUT8(0xFF) OUTPUT8(0x14)
				OUTPUT8(0x25)
				OUTPUT32((DWORD)(ULONG_PTR)&outputFunction);
				OUTPUT8(0x48) OUTPUT8(0x89) OUTPUT8(0xF1) OUTPUT8(0x48)
				OUTPUT8(0x89) OUTPUT8(0xFA)
				#endif
				code++;
				break;
			case ',':
				// TODO
				code++;
				break;
			case '[':
			{
				int pza, pm;
				if (PatternZero())
				{
					code += 3;
					OUTPUT8(0x48) OUTPUT8(0x31) OUTPUT8(0xC9)
				}
				else if (pza = PatternZeroAdd())
				{
					code += 4 + pza * 2;
					OUTPUT8(0x00) OUTPUT8(0x8A)
					OUTPUT32(pza)
					OUTPUT8(0x30) OUTPUT8(0xC9)
				}
				else
				{
					code++;
					LABEL begin = jitted;
					// test cl, cl
					OUTPUT8(0x84) OUTPUT8(0xC9)
					// jz ???
					OUTPUT8(0x0F) OUTPUT8(0x84)
					LPDWORD tbaOffset = (LPDWORD)jitted;
					OUTPUT32(0x00000000)
					LABEL calcBegin = jitted;
					Compile(TRUE, begin);
					*tbaOffset = jitted - calcBegin;
				}
				break;
			}
			case ']':
				code++;
				if (isLoop)
				{
					#define OFFSET(x) (DWORD)((x) - 4 - jitted)
					#define OUTJMP(x) OUTPUT8(0xE9) OUTPUT32(OFFSET(x))
					OUTJMP(loopStartOffset)
					#undef OUTJMP
					#undef OFFSET
					return;
				}
				break;
			case '\0':
				if (!isLoop)
				{
					// mov rsp, rbp
					// pop rbp
					// pop rdi
					// pop rsi
					// ret
					OUTPUT8(0x5D) OUTPUT16(0x5E5F)
					OUTPUT8(0xC3)
				}
				return;
			default:
				code++;
		}
	}
}

int WINAPI WinMain(
	HINSTANCE hInstance, 
	HINSTANCE hPrevInstance, 
    LPSTR lpCmdLine, 
	int nCmdShow)
{
	stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	int nArgs;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	HANDLE read;
	if ((read = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, 
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{
		MessageBoxA(NULL, "Unable to open file", "", MB_ICONERROR);
		return 0;
	}
	int size = GetFileSize(read, NULL);
	DWORD dwRead;
	code = HeapAlloc(GetProcessHeap(), 0, size + 1);
	ReadFile(read, code, size, &dwRead, NULL);
	code[size] = '\0';
	// Allocate space for generated code and make it executable
	jitted = jittedStart = VirtualAlloc(NULL, size * 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	
	outputFunction = ConsoleOutput;
	Compile(FALSE, 0u);
	
	// Fire up generated code!
	memory = VirtualAlloc(NULL, 30000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	((VOID(*)(PULONGLONG mem))jittedStart)(memory);
	
	// Flush final stdout buffer
	DWORD dwWritten;
	WriteConsoleA(stdout, buffer, bufferPos, &dwWritten, NULL);
	
	// Clean up
	VirtualFree(jittedStart, size * 64, MEM_RELEASE);
	VirtualFree(memory, 30000, MEM_RELEASE);
}
