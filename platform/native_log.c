#include "platform/native_log.h"

#include <macros.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include "platform/native_win32.h"
#endif

global_variable FILE *s_logStream = NULL;
global_variable char s_logPath[512]; // TODO(aalhendi): yeah this is an issue waiting to happen. w/e

// --crash-test. It is here because the log lives here and the crash report
// is nothing but a line in the log that nobody writes voluntarily.
int g_cfg_crashTest = 0;

// --no-sky, see game/DrawSky.c.
int g_cfg_noSky = 0;

internal void Platform_LogWrite(FILE *consoleStream, const char *text)
{
	FILE *stream = (consoleStream != NULL) ? consoleStream : stdout;

#ifdef _WIN32
	// Only when somebody is listening (2026-09-17). OutputDebugStringA without a debugger
	// is a system call per line that reaches nobody; a test run on
	// 2026-09-15 carried 4,875 lines "submitted ... mid-frame" - one per race frame.
	// IsDebuggerPresent reads a flag in the process block.
	if (IsDebuggerPresent())
	{
		OutputDebugStringA(text);
	}
#endif

	fputs(text, stream);

	if (s_logStream != NULL)
	{
		fputs(text, s_logStream);
		fflush(s_logStream);
	}
}

internal void Platform_LogV(FILE *consoleStream, const char *fmt, va_list args)
{
	char text[4096];
	int written = vsnprintf(text, sizeof(text), fmt, args);

	if (written < 0)
	{
		return;
	}

	text[sizeof(text) - 1] = '\0';
	Platform_LogWrite(consoleStream, text);
}

int Platform_LogSetPath(const char *path)
{
	if (s_logStream != NULL)
	{
		return 0;
	}

	if ((path == NULL) || (path[0] == '\0'))
	{
		s_logPath[0] = '\0';
		return 1;
	}

	int written = snprintf(s_logPath, sizeof(s_logPath), "%s", path);
	if ((written < 0) || ((size_t)written >= sizeof(s_logPath)))
	{
		s_logPath[0] = '\0';
		fprintf(stderr, "[CTR Native] Error: log path is too long\n");
		return 0;
	}

	return 1;
}

const char *Platform_LogGetPath(void)
{
	return s_logPath;
}

// Beta 0 (2026-09-30): the standard log is no longer overwritten at every
// start. Every start writes logs/<name> YYYY-MM-DD HH-MM-SS.log,
// the last NATIVE_LOG_KEEP remain. An explicit path (--log,
// --record) stays exactly as it is given - measuring runs depend on it.
#define NATIVE_LOG_DIR  "logs"
#define NATIVE_LOG_KEEP 5

#if defined(_WIN32)
internal int Platform_LogNameCompare(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

// Deletes the oldest "<appName> *.log" in logs/ until keep - 1 remain:
// room for the new one. The timestamp in the name sorts by time.
internal void Platform_LogPrune(const char *appName, int keep)
{
	char pattern[512];
	char names[64][260];
	int count = 0;
	WIN32_FIND_DATAA found;
	HANDLE find;
	int i;

	snprintf(pattern, sizeof(pattern), "%s\\%s *.log", NATIVE_LOG_DIR, appName);
	find = FindFirstFileA(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
	{
		return;
	}

	do
	{
		if (((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) && (count < 64))
		{
			snprintf(names[count], sizeof(names[count]), "%s", found.cFileName);
			count++;
		}
	} while (FindNextFileA(find, &found));
	FindClose(find);

	qsort(names, (size_t)count, sizeof(names[0]), Platform_LogNameCompare);

	for (i = 0; i < (count - (keep - 1)); i++)
	{
		char path[512];

		snprintf(path, sizeof(path), "%s\\%s", NATIVE_LOG_DIR, names[i]);
		DeleteFileA(path); // a log that is still open (second instance) simply stays
	}
}
#endif

void Platform_LogInit(const char *appName)
{
	if (s_logPath[0] == '\0')
	{
		time_t now = time(NULL);
		struct tm *local = localtime(&now);
		char stamp[32] = "0000-00-00 00-00-00";
		int written;

		if (local != NULL)
		{
			strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M-%S", local);
		}

#if defined(_WIN32)
		CreateDirectoryA(NATIVE_LOG_DIR, NULL);
		Platform_LogPrune(appName, NATIVE_LOG_KEEP);
		written = snprintf(s_logPath, sizeof(s_logPath), "%s\\%s %s.log", NATIVE_LOG_DIR, appName, stamp);
#else
		written = snprintf(s_logPath, sizeof(s_logPath), "%s %s.log", appName, stamp);
#endif

		if ((written < 0) || ((size_t)written >= sizeof(s_logPath)))
		{
			fprintf(stderr, "[CTR Native] Error: log filename is too long\n");
			s_logPath[0] = '\0';
			return;
		}
	}

	s_logStream = fopen(s_logPath, "wb");

	if (s_logStream == NULL)
	{
		fprintf(stderr, "[CTR Native] Error: cannot create log file '%s'\n", s_logPath);
	}
}

void Platform_LogShutdown(void)
{
	Platform_LogWarn("---- LOG CLOSED ----\n");

	if (s_logStream != NULL)
	{
		fclose(s_logStream);
	}

	s_logStream = NULL;
}

void Platform_LogFlush(void)
{
	if (s_logStream != NULL)
	{
		fflush(s_logStream);
	}
}

void Platform_Log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stdout, fmt, args);
	va_end(args);
}

void Platform_LogWarn(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stdout, fmt, args);
	va_end(args);
}

void Platform_LogError(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	Platform_LogV(stderr, fmt, args);
	va_end(args);
}
