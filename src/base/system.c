/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "system.h"

#include <sys/stat.h>
#include <sys/types.h>

#if defined(CONF_FAMILY_UNIX)
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

/* unix net includes */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <dirent.h>

#if defined(CONF_PLATFORM_MACOS)
#include <Carbon/Carbon.h>
#endif

#elif defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 /* required for mingw to get getaddrinfo to work */
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <wincrypt.h>
#else
#error NOT IMPLEMENTED
#endif

#if defined(CONF_PLATFORM_SOLARIS)
#include <sys/filio.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

IOHANDLE io_stdin()
{
	return (IOHANDLE)stdin;
}
IOHANDLE io_stdout() { return (IOHANDLE)stdout; }
IOHANDLE io_stderr() { return (IOHANDLE)stderr; }

typedef struct
{
	DBG_LOGGER logger;
	DBG_LOGGER_FINISH finish;
	void *user;
} DBG_LOGGER_DATA;

static DBG_LOGGER_DATA loggers[16];
static int num_loggers = 0;

static NETSTATS network_stats = {0};

static NETSOCKET invalid_socket = {NETTYPE_INVALID, -1, -1};

void dbg_assert_imp(const char *filename, int line, int test, const char *msg)
{
	if(!test)
	{
		dbg_msg("assert", "%s(%d): %s", filename, line, msg);
		dbg_break_imp();
	}
}

void dbg_break_imp(void)
{
#ifdef __GNUC__
	__builtin_trap();
#else
	*((volatile unsigned *)0) = 0x0;
#endif
}

void dbg_msg(const char *sys, const char *fmt, ...)
{
	va_list args;
	char *msg;
	int len;

	char str[1024 * 4];
	int i;

	char timestr[80];
	str_timestamp_format(timestr, sizeof(timestr), FORMAT_SPACE);

	str_format(str, sizeof(str), "[%s][%s]: ", timestr, sys);

	len = str_length(str);
	msg = (char *)str + len;

	va_start(args, fmt);
#if defined(CONF_FAMILY_WINDOWS)
	_vsnprintf(msg, sizeof(str) - len, fmt, args);
#else
	vsnprintf(msg, sizeof(str) - len, fmt, args);
#endif
	va_end(args);

	for(i = 0; i < num_loggers; i++)
		loggers[i].logger(str, loggers[i].user);
}

#if defined(CONF_FAMILY_WINDOWS)
static void logger_debugger(const char *line, void *user)
{
	(void)user;
	OutputDebugString(line);
	OutputDebugString("\n");
}
#endif

static void logger_file(const char *line, void *user)
{
	ASYNCIO *logfile = (ASYNCIO *)user;
	aio_lock(logfile);
	aio_write_unlocked(logfile, line, str_length(line));
	aio_write_newline_unlocked(logfile);
	aio_unlock(logfile);
}

#if defined(CONF_FAMILY_WINDOWS)
static void logger_stdout_sync(const char *line, void *user)
{
	size_t length = str_length(line);
	wchar_t *wide = malloc(length * sizeof(*wide));
	const char *p = line;
	int wlen = 0;
	HANDLE console;

	(void)user;
	mem_zero(wide, length * sizeof *wide);

	for(int codepoint = 0; (codepoint = str_utf8_decode(&p)); wlen++)
	{
		char u16[4] = {0};

		if(codepoint < 0)
		{
			free(wide);
			return;
		}

		if(str_utf16le_encode(u16, codepoint) != 2)
		{
			free(wide);
			return;
		}

		mem_copy(&wide[wlen], u16, 2);
	}

	console = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleW(console, wide, wlen, NULL, NULL);
	WriteConsoleA(console, "\n", 1, NULL, NULL);
	free(wide);
}
#endif

static void logger_stdout_finish(void *user)
{
	ASYNCIO *logfile = (ASYNCIO *)user;
	aio_wait(logfile);
	aio_free(logfile);
}

static void logger_file_finish(void *user)
{
	ASYNCIO *logfile = (ASYNCIO *)user;
	aio_close(logfile);
	logger_stdout_finish(user);
}

static void dbg_logger_finish(void)
{
	int i;
	for(i = 0; i < num_loggers; i++)
	{
		if(loggers[i].finish)
		{
			loggers[i].finish(loggers[i].user);
		}
	}
}

void dbg_logger(DBG_LOGGER logger, DBG_LOGGER_FINISH finish, void *user)
{
	DBG_LOGGER_DATA data;
	if(num_loggers == 0)
	{
		atexit(dbg_logger_finish);
	}
	data.logger = logger;
	data.finish = finish;
	data.user = user;
	loggers[num_loggers] = data;
	num_loggers++;
}

void dbg_logger_stdout(void)
{
#if defined(CONF_FAMILY_WINDOWS)
	dbg_logger(logger_stdout_sync, 0, 0);
#else
	dbg_logger(logger_file, logger_stdout_finish, aio_new(io_stdout()));
#endif
}

void dbg_logger_debugger(void)
{
#if defined(CONF_FAMILY_WINDOWS)
	dbg_logger(logger_debugger, 0, 0);
#endif
}

void dbg_logger_file(const char *filename)
{
	IOHANDLE logfile = io_open(filename, IOFLAG_WRITE);
	if(logfile)
		dbg_logger(logger_file, logger_file_finish, aio_new(logfile));
	else
		dbg_msg("dbg/logger", "failed to open '%s' for logging", filename);
}
/* */

void mem_copy(void *dest, const void *source, unsigned size)
{
	memcpy(dest, source, size);
}

void mem_move(void *dest, const void *source, unsigned size)
{
	memmove(dest, source, size);
}

void mem_zero(void *block, unsigned size)
{
	memset(block, 0, size);
}

IOHANDLE io_open(const char *filename, int flags)
{
	if(flags == IOFLAG_READ)
	{
	#if defined(CONF_FAMILY_WINDOWS)
		// check for filename case sensitive
		WIN32_FIND_DATA finddata;
		HANDLE handle;
		int length;

		length = str_length(filename);
		if(!filename || !length || filename[length-1] == '\\')
			return 0x0;
		handle = FindFirstFile(filename, &finddata);
		if(handle == INVALID_HANDLE_VALUE)
			return 0x0;
		else if(str_comp(filename+length-str_length(finddata.cFileName), finddata.cFileName) != 0)
		{
			FindClose(handle);
			return 0x0;
		}
		FindClose(handle);
	#endif
		return (IOHANDLE)fopen(filename, "rb");
	}
	if(flags == IOFLAG_WRITE)
		return (IOHANDLE)fopen(filename, "wb");
	return 0x0;
}

unsigned io_read(IOHANDLE io, void *buffer, unsigned size)
{
	return fread(buffer, 1, size, (FILE *)io);
}

unsigned io_skip(IOHANDLE io, int size)
{
	fseek((FILE *)io, size, SEEK_CUR);
	return size;
}

int io_seek(IOHANDLE io, int offset, int origin)
{
	int real_origin;

	switch(origin)
	{
	case IOSEEK_START:
		real_origin = SEEK_SET;
		break;
	case IOSEEK_CUR:
		real_origin = SEEK_CUR;
		break;
	case IOSEEK_END:
		real_origin = SEEK_END;
		break;
	default:
		return -1;
	}

	return fseek((FILE *)io, offset, real_origin);
}

long int io_tell(IOHANDLE io)
{
	return ftell((FILE *)io);
}

long int io_length(IOHANDLE io)
{
	long int length;
	io_seek(io, 0, IOSEEK_END);
	length = io_tell(io);
	io_seek(io, 0, IOSEEK_START);
	return length;
}

int io_error(IOHANDLE io)
{
	return ferror((FILE *)io);
}

unsigned io_write(IOHANDLE io, const void *buffer, unsigned size)
{
	return fwrite(buffer, 1, size, (FILE *)io);
}

unsigned io_write_newline(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return fwrite("\r\n", 1, 2, (FILE *)io);
#else
	return fwrite("\n", 1, 1, (FILE *)io);
#endif
}

int io_close(IOHANDLE io)
{
	fclose((FILE*)io);
	return 1;
}

int io_flush(IOHANDLE io)
{
	return fflush((FILE *)io);
}

#define ASYNC_BUFSIZE 8 * 1024
#define ASYNC_LOCAL_BUFSIZE 64 * 1024

// TODO: Use Thread Safety Analysis when this file is converted to C++
struct ASYNCIO
{
	LOCK lock;
	IOHANDLE io;
	SEMAPHORE sphore;
	void *thread;

	unsigned char *buffer;
	unsigned int buffer_size;
	unsigned int read_pos;
	unsigned int write_pos;

	int error;
	unsigned char finish;
	unsigned char refcount;
};

enum
{
	ASYNCIO_RUNNING,
	ASYNCIO_CLOSE,
	ASYNCIO_EXIT,
};

struct BUFFERS
{
	unsigned char *buf1;
	unsigned int len1;
	unsigned char *buf2;
	unsigned int len2;
};

static void buffer_ptrs(ASYNCIO *aio, struct BUFFERS *buffers)
{
	mem_zero(buffers, sizeof(*buffers));
	if(aio->read_pos < aio->write_pos)
	{
		buffers->buf1 = aio->buffer + aio->read_pos;
		buffers->len1 = aio->write_pos - aio->read_pos;
	}
	else if(aio->read_pos > aio->write_pos)
	{
		buffers->buf1 = aio->buffer + aio->read_pos;
		buffers->len1 = aio->buffer_size - aio->read_pos;
		buffers->buf2 = aio->buffer;
		buffers->len2 = aio->write_pos;
	}
}

static void aio_handle_free_and_unlock(ASYNCIO *aio) RELEASE(aio->lock)
{
	int do_free;
	aio->refcount--;

	do_free = aio->refcount == 0;
	lock_unlock(aio->lock);
	if(do_free)
	{
		free(aio->buffer);
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
	}
}

static void aio_thread(void *user)
{
	ASYNCIO *aio = user;

	lock_wait(aio->lock);
	while(1)
	{
		struct BUFFERS buffers;
		int result_io_error;
		unsigned char local_buffer[ASYNC_LOCAL_BUFSIZE];
		unsigned int local_buffer_len = 0;

		if(aio->read_pos == aio->write_pos)
		{
			if(aio->finish != ASYNCIO_RUNNING)
			{
				if(aio->finish == ASYNCIO_CLOSE)
				{
					io_close(aio->io);
				}
				aio_handle_free_and_unlock(aio);
				break;
			}
			lock_unlock(aio->lock);
			sphore_wait(&aio->sphore);
			lock_wait(aio->lock);
			continue;
		}

		buffer_ptrs(aio, &buffers);
		if(buffers.buf1)
		{
			if(buffers.len1 > sizeof(local_buffer) - local_buffer_len)
			{
				buffers.len1 = sizeof(local_buffer) - local_buffer_len;
			}
			mem_copy(local_buffer + local_buffer_len, buffers.buf1, buffers.len1);
			local_buffer_len += buffers.len1;
			if(buffers.buf2)
			{
				if(buffers.len2 > sizeof(local_buffer) - local_buffer_len)
				{
					buffers.len2 = sizeof(local_buffer) - local_buffer_len;
				}
				mem_copy(local_buffer + local_buffer_len, buffers.buf2, buffers.len2);
				local_buffer_len += buffers.len2;
			}
		}
		aio->read_pos = (aio->read_pos + buffers.len1 + buffers.len2) % aio->buffer_size;
		lock_unlock(aio->lock);

		io_write(aio->io, local_buffer, local_buffer_len);
		io_flush(aio->io);
		result_io_error = io_error(aio->io);

		lock_wait(aio->lock);
		aio->error = result_io_error;
	}
}

ASYNCIO *aio_new(IOHANDLE io)
{
	ASYNCIO *aio = malloc(sizeof(*aio));
	if(!aio)
	{
		return 0;
	}
	aio->io = io;
	aio->lock = lock_create();
	sphore_init(&aio->sphore);
	aio->thread = 0;

	aio->buffer = malloc(ASYNC_BUFSIZE);
	if(!aio->buffer)
	{
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
		return 0;
	}
	aio->buffer_size = ASYNC_BUFSIZE;
	aio->read_pos = 0;
	aio->write_pos = 0;
	aio->error = 0;
	aio->finish = ASYNCIO_RUNNING;
	aio->refcount = 2;

	aio->thread = thread_init(aio_thread, aio, "aio");
	if(!aio->thread)
	{
		free(aio->buffer);
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
		return 0;
	}
	return aio;
}

static unsigned int buffer_len(ASYNCIO *aio)
{
	if(aio->write_pos >= aio->read_pos)
	{
		return aio->write_pos - aio->read_pos;
	}
	else
	{
		return aio->buffer_size + aio->write_pos - aio->read_pos;
	}
}

static unsigned int next_buffer_size(unsigned int cur_size, unsigned int need_size)
{
	while(cur_size < need_size)
	{
		cur_size *= 2;
	}
	return cur_size;
}

void aio_lock(ASYNCIO *aio) ACQUIRE(aio->lock)
{
	lock_wait(aio->lock);
}

void aio_unlock(ASYNCIO *aio) RELEASE(aio->lock)
{
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
}

void aio_write_unlocked(ASYNCIO *aio, const void *buffer, unsigned size)
{
	unsigned int remaining;
	remaining = aio->buffer_size - buffer_len(aio);

	// Don't allow full queue to distinguish between empty and full queue.
	if(size < remaining)
	{
		unsigned int remaining_contiguous = aio->buffer_size - aio->write_pos;
		if(size > remaining_contiguous)
		{
			mem_copy(aio->buffer + aio->write_pos, buffer, remaining_contiguous);
			size -= remaining_contiguous;
			buffer = ((unsigned char *)buffer) + remaining_contiguous;
			aio->write_pos = 0;
		}
		mem_copy(aio->buffer + aio->write_pos, buffer, size);
		aio->write_pos = (aio->write_pos + size) % aio->buffer_size;
	}
	else
	{
		// Add 1 so the new buffer isn't completely filled.
		unsigned int new_written = buffer_len(aio) + size + 1;
		unsigned int next_size = next_buffer_size(aio->buffer_size, new_written);
		unsigned int next_len = 0;
		unsigned char *next_buffer = malloc(next_size);

		struct BUFFERS buffers;
		buffer_ptrs(aio, &buffers);
		if(buffers.buf1)
		{
			mem_copy(next_buffer + next_len, buffers.buf1, buffers.len1);
			next_len += buffers.len1;
			if(buffers.buf2)
			{
				mem_copy(next_buffer + next_len, buffers.buf2, buffers.len2);
				next_len += buffers.len2;
			}
		}
		mem_copy(next_buffer + next_len, buffer, size);
		next_len += size;

		free(aio->buffer);
		aio->buffer = next_buffer;
		aio->buffer_size = next_size;
		aio->read_pos = 0;
		aio->write_pos = next_len;
	}
}

void aio_write(ASYNCIO *aio, const void *buffer, unsigned size)
{
	aio_lock(aio);
	aio_write_unlocked(aio, buffer, size);
	aio_unlock(aio);
}

void aio_write_newline_unlocked(ASYNCIO *aio)
{
#if defined(CONF_FAMILY_WINDOWS)
	aio_write_unlocked(aio, "\r\n", 2);
#else
	aio_write_unlocked(aio, "\n", 1);
#endif
}

void aio_write_newline(ASYNCIO *aio)
{
	aio_lock(aio);
	aio_write_newline_unlocked(aio);
	aio_unlock(aio);
}

int aio_error(ASYNCIO *aio)
{
	int result;
	lock_wait(aio->lock);
	result = aio->error;
	lock_unlock(aio->lock);
	return result;
}

void aio_free(ASYNCIO *aio)
{
	lock_wait(aio->lock);
	if(aio->thread)
	{
		thread_detach(aio->thread);
		aio->thread = 0;
	}
	aio_handle_free_and_unlock(aio);
}

void aio_close(ASYNCIO *aio)
{
	lock_wait(aio->lock);
	aio->finish = ASYNCIO_CLOSE;
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
}

void aio_wait(ASYNCIO *aio)
{
	void *thread;
	lock_wait(aio->lock);
	thread = aio->thread;
	aio->thread = 0;
	if(aio->finish == ASYNCIO_RUNNING)
	{
		aio->finish = ASYNCIO_EXIT;
	}
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
	thread_wait(thread);
}

struct THREAD_RUN
{
	void (*threadfunc)(void *);
	void *u;
};

#if defined(CONF_FAMILY_UNIX)
static void *thread_run(void *user)
#elif defined(CONF_FAMILY_WINDOWS)
static unsigned long __stdcall thread_run(void *user)
#else
#error not implemented
#endif
{
	struct THREAD_RUN *data = user;
	void (*threadfunc)(void *) = data->threadfunc;
	void *u = data->u;
	free(data);
	threadfunc(u);
	return 0;
}

void *thread_init(void (*threadfunc)(void *), void *u, const char *name)
{
	struct THREAD_RUN *data = malloc(sizeof(*data));
	data->threadfunc = threadfunc;
	data->u = u;
#if defined(CONF_FAMILY_UNIX)
	{
		pthread_t id;
		int result = pthread_create(&id, NULL, thread_run, data);
		if(result != 0)
		{
			dbg_msg("thread", "creating %s thread failed: %d", name, result);
			return 0;
		}
		return (void *)id;
	}
#elif defined(CONF_FAMILY_WINDOWS)
	return CreateThread(NULL, 0, thread_run, data, 0, NULL);
#else
#error not implemented
#endif
}

void thread_wait(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	int result = pthread_join((pthread_t)thread, NULL);
	if(result != 0)
		dbg_msg("thread", "!! %d", result);
#elif defined(CONF_FAMILY_WINDOWS)
	WaitForSingleObject((HANDLE)thread, INFINITE);
	CloseHandle(thread);
#else
#error not implemented
#endif
}

void thread_yield(void)
{
#if defined(CONF_FAMILY_UNIX)
	int result = sched_yield();
	if(result != 0)
		dbg_msg("thread", "yield failed: %d", errno);
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(0);
#else
#error not implemented
#endif
}

void thread_sleep(int microseconds)
{
#if defined(CONF_FAMILY_UNIX)
	int result = usleep(microseconds);
	/* ignore signal interruption */
	if(result == -1 && errno != EINTR)
		dbg_msg("thread", "sleep failed: %d", errno);
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(microseconds / 1000);
#else
#error not implemented
#endif
}

void thread_detach(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	int result = pthread_detach((pthread_t)(thread));
	if(result != 0)
		dbg_msg("thread", "detach failed: %d", result);
#elif defined(CONF_FAMILY_WINDOWS)
	CloseHandle(thread);
#else
#error not implemented
#endif
}

void *thread_init_and_detach(void (*threadfunc)(void *), void *u, const char *name)
{
	void *thread = thread_init(threadfunc, u, name);
	if(thread)
		thread_detach(thread);
	return thread;
}

#if defined(CONF_FAMILY_UNIX)
typedef pthread_mutex_t LOCKINTERNAL;
#elif defined(CONF_FAMILY_WINDOWS)
typedef CRITICAL_SECTION LOCKINTERNAL;
#else
#error not implemented on this platform
#endif

LOCK lock_create(void)
{
	LOCKINTERNAL *lock = (LOCKINTERNAL *)malloc(sizeof(*lock));
#if defined(CONF_FAMILY_UNIX)
	int result;
#endif

	if(!lock)
		return 0;

#if defined(CONF_FAMILY_UNIX)
	result = pthread_mutex_init(lock, 0x0);
	if(result != 0)
	{
		dbg_msg("lock", "init failed: %d", result);
		free(lock);
		return 0;
	}
#elif defined(CONF_FAMILY_WINDOWS)
	InitializeCriticalSection((LPCRITICAL_SECTION)lock);
#else
#error not implemented on this platform
#endif
	return (LOCK)lock;
}

void lock_destroy(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	int result = pthread_mutex_destroy((LOCKINTERNAL *)lock);
	if(result != 0)
		dbg_msg("lock", "destroy failed: %d", result);
#elif defined(CONF_FAMILY_WINDOWS)
	DeleteCriticalSection((LPCRITICAL_SECTION)lock);
#else
#error not implemented on this platform
#endif
	free(lock);
}

int lock_trylock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	return pthread_mutex_trylock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	return !TryEnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
#error not implemented on this platform
#endif
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
void lock_wait(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	int result = pthread_mutex_lock((LOCKINTERNAL *)lock);
	if(result != 0)
		dbg_msg("lock", "lock failed: %d", result);
#elif defined(CONF_FAMILY_WINDOWS)
	EnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
#error not implemented on this platform
#endif
}

void lock_unlock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	int result = pthread_mutex_unlock((LOCKINTERNAL *)lock);
	if(result != 0)
		dbg_msg("lock", "unlock failed: %d", result);
#elif defined(CONF_FAMILY_WINDOWS)
	LeaveCriticalSection((LPCRITICAL_SECTION)lock);
#else
#error not implemented on this platform
#endif
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined(CONF_FAMILY_WINDOWS)
void sphore_init(SEMAPHORE *sem)
{
	*sem = CreateSemaphore(0, 0, 10000, 0);
}
void sphore_wait(SEMAPHORE *sem) { WaitForSingleObject((HANDLE)*sem, INFINITE); }
void sphore_signal(SEMAPHORE *sem) { ReleaseSemaphore((HANDLE)*sem, 1, NULL); }
void sphore_destroy(SEMAPHORE *sem) { CloseHandle((HANDLE)*sem); }
#elif defined(CONF_PLATFORM_MACOS)
void sphore_init(SEMAPHORE *sem)
{
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "/%d-ddnet.tw-%p", pid(), (void *)sem);
	*sem = sem_open(aBuf, O_CREAT | O_EXCL, S_IRWXU | S_IRWXG, 0);
}
void sphore_wait(SEMAPHORE *sem) { sem_wait(*sem); }
void sphore_signal(SEMAPHORE *sem) { sem_post(*sem); }
void sphore_destroy(SEMAPHORE *sem)
{
	char aBuf[64];
	sem_close(*sem);
	str_format(aBuf, sizeof(aBuf), "/%d-ddnet.tw-%p", pid(), (void *)sem);
	sem_unlink(aBuf);
}
#elif defined(CONF_FAMILY_UNIX)
void sphore_init(SEMAPHORE *sem)
{
	if(sem_init(sem, 0, 0) != 0)
		dbg_msg("sphore", "init failed: %d", errno);
}

void sphore_wait(SEMAPHORE *sem)
{
	if(sem_wait(sem) != 0)
		dbg_msg("sphore", "wait failed: %d", errno);
}

void sphore_signal(SEMAPHORE *sem)
{
	if(sem_post(sem) != 0)
		dbg_msg("sphore", "post failed: %d", errno);
}
void sphore_destroy(SEMAPHORE *sem)
{
	if(sem_destroy(sem) != 0)
		dbg_msg("sphore", "destroy failed: %d", errno);
}
#endif

static int new_tick = -1;

void set_new_tick(void)
{
	new_tick = 1;
}

/* -----  time ----- */
int64 time_get_impl(void)
{
	static int64 last = 0;
	{
#if defined(CONF_PLATFORM_MACOS)
		static int got_timebase = 0;
		mach_timebase_info_data_t timebase;
		uint64 time;
		uint64 q;
		uint64 r;
		if(!got_timebase)
		{
			mach_timebase_info(&timebase);
		}
		time = mach_absolute_time();
		q = time / timebase.denom;
		r = time % timebase.denom;
		last = q * timebase.numer + r * timebase.numer / timebase.denom;
		return last;
#elif defined(CONF_FAMILY_UNIX)
		struct timespec spec;
		if(clock_gettime(CLOCK_MONOTONIC, &spec) != 0)
		{
			dbg_msg("clock", "gettime failed: %d", errno);
			return 0;
		}
		last = (int64)spec.tv_sec * (int64)1000000 + (int64)spec.tv_nsec / 1000;
		return last;
#elif defined(CONF_FAMILY_WINDOWS)
		int64 t;
		QueryPerformanceCounter((PLARGE_INTEGER)&t);
		if(t < last) /* for some reason, QPC can return values in the past */
			return last;
		last = t;
		return t;
#else
#error not implemented
#endif
	}
}

int64 time_get(void)
{
	static int64 last = 0;
	if(new_tick == 0)
		return last;
	if(new_tick != -1)
		new_tick = 0;

	last = time_get_impl();
	return last;
}

int64 time_freq(void)
{
#if defined(CONF_PLATFORM_MACOS)
	return 1000000000;
#elif defined(CONF_FAMILY_UNIX)
	return 1000000;
#elif defined(CONF_FAMILY_WINDOWS)
	int64 t;
	QueryPerformanceFrequency((PLARGE_INTEGER)&t);
	return t;
#else
#error not implemented
#endif
}

int64 time_get_microseconds(void)
{
#if defined(CONF_FAMILY_WINDOWS)
	return (time_get_impl() * (int64)1000000) / time_freq();
#else
	return time_get_impl() / (time_freq() / 1000 / 1000);
#endif
}

/* -----  network ----- */
static void netaddr_to_sockaddr_in(const NETADDR *src, struct sockaddr_in *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in));
	if(src->type != NETTYPE_IPV4)
	{
		dbg_msg("system", "couldn't convert NETADDR of type %d to ipv4", src->type);
		return;
	}

	dest->sin_family = AF_INET;
	dest->sin_port = htons(src->port);
	mem_copy(&dest->sin_addr.s_addr, src->ip, 4);
}

static void netaddr_to_sockaddr_in6(const NETADDR *src, struct sockaddr_in6 *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in6));
	if(src->type != NETTYPE_IPV6)
	{
		dbg_msg("system", "couldn't not convert NETADDR of type %d to ipv6", src->type);
		return;
	}

	dest->sin6_family = AF_INET6;
	dest->sin6_port = htons(src->port);
	mem_copy(&dest->sin6_addr.s6_addr, src->ip, 16);
}

static void sockaddr_to_netaddr(const struct sockaddr *src, NETADDR *dst)
{
	if(src->sa_family == AF_INET)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV4;
		dst->port = htons(((struct sockaddr_in *)src)->sin_port);
		mem_copy(dst->ip, &((struct sockaddr_in *)src)->sin_addr.s_addr, 4);
	}
	else if(src->sa_family == AF_INET6)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV6;
		dst->port = htons(((struct sockaddr_in6 *)src)->sin6_port);
		mem_copy(dst->ip, &((struct sockaddr_in6 *)src)->sin6_addr.s6_addr, 16);
	}
	else
	{
		mem_zero(dst, sizeof(struct sockaddr));
		dbg_msg("system", "couldn't convert sockaddr of family %d", src->sa_family);
	}
}

int net_addr_comp(const NETADDR *a, const NETADDR *b)
{
	return mem_comp(a, b, sizeof(NETADDR));
}

int net_addr_comp_noport(const NETADDR *a, const NETADDR *b)
{
	NETADDR ta = *a, tb = *b;
	ta.port = tb.port = 0;

	return net_addr_comp(&ta, &tb);
}

void net_addr_str(const NETADDR *addr, char *string, int max_length, int add_port)
{
	if(addr->type == NETTYPE_IPV4)
	{
		if(add_port != 0)
			str_format(string, max_length, "%d.%d.%d.%d:%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3], addr->port);
		else
			str_format(string, max_length, "%d.%d.%d.%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
	}
	else if(addr->type == NETTYPE_IPV6)
	{
		if(add_port != 0)
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]:%d",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15],
				addr->port);
		else
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15]);
	}
	else
		str_format(string, max_length, "unknown type %d", addr->type);
}

static int priv_net_extract(const char *hostname, char *host, int max_host, int *port)
{
	int i;

	*port = 0;
	host[0] = 0;

	if(hostname[0] == '[')
	{
		// ipv6 mode
		for(i = 1; i < max_host && hostname[i] && hostname[i] != ']'; i++)
			host[i - 1] = hostname[i];
		host[i - 1] = 0;
		if(hostname[i] != ']') // malformatted
			return -1;

		i++;
		if(hostname[i] == ':')
			*port = atol(hostname + i + 1);
	}
	else
	{
		// generic mode (ipv4, hostname etc)
		for(i = 0; i < max_host - 1 && hostname[i] && hostname[i] != ':'; i++)
			host[i] = hostname[i];
		host[i] = 0;

		if(hostname[i] == ':')
			*port = atol(hostname + i + 1);
	}

	return 0;
}

int net_host_lookup(const char *hostname, NETADDR *addr, int types)
{
	struct addrinfo hints;
	struct addrinfo *result;
	int e;
	char host[256];
	int port = 0;

	if(priv_net_extract(hostname, host, sizeof(host), &port))
		return -1;
	/*
	dbg_msg("host lookup", "host='%s' port=%d %d", host, port, types);
	*/

	mem_zero(&hints, sizeof(hints));

	hints.ai_family = AF_UNSPEC;

	if(types == NETTYPE_IPV4)
		hints.ai_family = AF_INET;
	else if(types == NETTYPE_IPV6)
		hints.ai_family = AF_INET6;

	e = getaddrinfo(host, NULL, &hints, &result);
	if(e != 0 || !result)
		return -1;

	sockaddr_to_netaddr(result->ai_addr, addr);
	freeaddrinfo(result);
	addr->port = port;
	return 0;
}

static int parse_int(int *out, const char **str)
{
	int i = 0;
	*out = 0;
	if(**str < '0' || **str > '9')
		return -1;

	i = **str - '0';
	(*str)++;

	while(1)
	{
		if(**str < '0' || **str > '9')
		{
			*out = i;
			return 0;
		}

		i = (i * 10) + (**str - '0');
		(*str)++;
	}

	return 0;
}

static int parse_char(char c, const char **str)
{
	if(**str != c)
		return -1;
	(*str)++;
	return 0;
}

static int parse_uint8(unsigned char *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0)
		return -1;
	if(i < 0 || i > 0xff)
		return -1;
	*out = i;
	return 0;
}

static int parse_uint16(unsigned short *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0)
		return -1;
	if(i < 0 || i > 0xffff)
		return -1;
	*out = i;
	return 0;
}

int net_addr_from_str(NETADDR *addr, const char *string)
{
	const char *str = string;
	mem_zero(addr, sizeof(NETADDR));

	if(str[0] == '[')
	{
		/* ipv6 */
		struct sockaddr_in6 sa6;
		char buf[128];
		int i;
		str++;
		for(i = 0; i < 127 && str[i] && str[i] != ']'; i++)
			buf[i] = str[i];
		buf[i] = 0;
		str += i;
#if defined(CONF_FAMILY_WINDOWS)
		{
			int size;
			sa6.sin6_family = AF_INET6;
			size = (int)sizeof(sa6);
			if(WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr *)&sa6, &size) != 0)
				return -1;
		}
#else
		if(inet_pton(AF_INET6, buf, &sa6) != 1)
			return -1;
#endif
		sockaddr_to_netaddr((struct sockaddr *)&sa6, addr);

		if(*str == ']')
		{
			str++;
			if(*str == ':')
			{
				str++;
				if(parse_uint16(&addr->port, &str))
					return -1;
			}
		}
		else
			return -1;

		return 0;
	}
	else
	{
		/* ipv4 */
		if(parse_uint8(&addr->ip[0], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[1], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[2], &str))
			return -1;
		if(parse_char('.', &str))
			return -1;
		if(parse_uint8(&addr->ip[3], &str))
			return -1;
		if(*str == ':')
		{
			str++;
			if(parse_uint16(&addr->port, &str))
				return -1;
		}

		addr->type = NETTYPE_IPV4;
	}

	return 0;
}

static void priv_net_close_socket(int sock)
{
#if defined(CONF_FAMILY_WINDOWS)
	closesocket(sock);
#else
	close(sock);
#endif
}

static int priv_net_close_all_sockets(NETSOCKET sock)
{
	/* close down ipv4 */
	if(sock.ipv4sock >= 0)
	{
		priv_net_close_socket(sock.ipv4sock);
		sock.ipv4sock = -1;
		sock.type &= ~NETTYPE_IPV4;
	}

	/* close down ipv6 */
	if(sock.ipv6sock >= 0)
	{
		priv_net_close_socket(sock.ipv6sock);
		sock.ipv6sock = -1;
		sock.type &= ~NETTYPE_IPV6;
	}
	return 0;
}

static int priv_net_create_socket(int domain, int type, struct sockaddr *addr, int sockaddrlen)
{
	int sock, e;

	/* create socket */
	sock = socket(domain, type, 0);
	if(sock < 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		char buf[128];
		int error = WSAGetLastError();
		if(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, buf, sizeof(buf), 0) == 0)
			buf[0] = 0;
		dbg_msg("net", "failed to create socket with domain %d and type %d (%d '%s')", domain, type, error, buf);
#else
		dbg_msg("net", "failed to create socket with domain %d and type %d (%d '%s')", domain, type, errno, strerror(errno));
#endif
		return -1;
	}

	/* set to IPv6 only if thats what we are creating */
#if defined(IPV6_V6ONLY)	/* windows sdk 6.1 and higher */
	if(domain == AF_INET6)
	{
		int ipv6only = 1;
		setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&ipv6only, sizeof(ipv6only));
	}
#endif
#if defined(CONF_FAMILY_WINDOWS)
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
		dbg_msg("net", "failed to setsockopt SO_REUSEADDR");
	}
#else
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0) {
		dbg_msg("net", "failed to setsockopt SO_REUSEPORT");
	}
#endif
	/* bind the socket */
	e = bind(sock, addr, sockaddrlen);
	if(e != 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		char buf[128];
		int error = WSAGetLastError();
		if(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, buf, sizeof(buf), 0) == 0)
			buf[0] = 0;
		dbg_msg("net", "failed to bind socket with domain %d and type %d (%d '%s')", domain, type, error, buf);
#else
		dbg_msg("net", "failed to bind socket with domain %d and type %d (%d '%s')", domain, type, errno, strerror(errno));
#endif
		priv_net_close_socket(sock);
		return -1;
	}

	/* return the newly created socket */
	return sock;
}

NETSOCKET net_udp_create(NETADDR bindaddr)
{
	NETSOCKET sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;
	int broadcast = 1;
	int recvsize = 65536;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV4;
			sock.ipv4sock = socket;

			/* set boardcast */
			setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

			/* set receive buffer size */
			setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvsize, sizeof(recvsize));
		}
	}

	if(bindaddr.type&NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV6;
			sock.ipv6sock = socket;

			/* set boardcast */
			setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

			/* set receive buffer size */
			setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvsize, sizeof(recvsize));
		}
	}

	/* set non-blocking */
	net_set_non_blocking(sock);

	/* return */
	return sock;
}

int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	int d = -1;

	if(addr->type & NETTYPE_IPV4)
	{
		if(sock.ipv4sock >= 0)
		{
			struct sockaddr_in sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin_port = htons(addr->port);
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = INADDR_BROADCAST;
			}
			else
				netaddr_to_sockaddr_in(addr, &sa);

			d = sendto((int)sock.ipv4sock, (const char *)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			dbg_msg("net", "can't send ipv4 traffic to this socket");
	}

	if(addr->type & NETTYPE_IPV6)
	{
		if(sock.ipv6sock >= 0)
		{
			struct sockaddr_in6 sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin6_port = htons(addr->port);
				sa.sin6_family = AF_INET6;
				sa.sin6_addr.s6_addr[0] = 0xff; /* multicast */
				sa.sin6_addr.s6_addr[1] = 0x02; /* link local scope */
				sa.sin6_addr.s6_addr[15] = 1; /* all nodes */
			}
			else
				netaddr_to_sockaddr_in6(addr, &sa);

			d = sendto((int)sock.ipv6sock, (const char *)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			dbg_msg("net", "can't send ipv6 traffic to this socket");
	}
	/*
	else
		dbg_msg("net", "can't send to network of type %d", addr->type);
		*/

	/*if(d < 0)
	{
		char addrstr[256];
		net_addr_str(addr, addrstr, sizeof(addrstr));

		dbg_msg("net", "sendto error (%d '%s')", errno, strerror(errno));
		dbg_msg("net", "\tsock = %d %x", sock, sock);
		dbg_msg("net", "\tsize = %d %x", size, size);
		dbg_msg("net", "\taddr = %s", addrstr);

	}*/
	network_stats.sent_bytes += size;
	network_stats.sent_packets++;
	return d;
}

int net_udp_recv(NETSOCKET sock, NETADDR *addr, void *data, int maxsize)
{
	char sockaddrbuf[128];
	socklen_t fromlen;// = sizeof(sockaddrbuf);
	int bytes = 0;

	if(bytes == 0 && sock.ipv4sock >= 0)
	{
		fromlen = sizeof(struct sockaddr_in);
		bytes = recvfrom(sock.ipv4sock, (char*)data, maxsize, 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
	}

	if(bytes <= 0 && sock.ipv6sock >= 0)
	{
		fromlen = sizeof(struct sockaddr_in6);
		bytes = recvfrom(sock.ipv6sock, (char*)data, maxsize, 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
	}

	if(bytes > 0)
	{
		sockaddr_to_netaddr((struct sockaddr *)&sockaddrbuf, addr);
		network_stats.recv_bytes += bytes;
		network_stats.recv_packets++;
		return bytes;
	}
	else if(bytes == 0)
		return 0;
	return -1; /* error */
}

int net_udp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

NETSOCKET net_tcp_create(NETADDR bindaddr)
{
	NETSOCKET sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV4;
			sock.ipv4sock = socket;
		}
	}

	if(bindaddr.type & NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV6;
			sock.ipv6sock = socket;
		}
	}

	/* return */
	return sock;
}

int net_set_non_blocking(NETSOCKET sock)
{
	unsigned long mode = 1;
	if(sock.ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	if(sock.ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	return 0;
}

int net_set_blocking(NETSOCKET sock)
{
	unsigned long mode = 0;
	if(sock.ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	if(sock.ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	return 0;
}

int net_tcp_listen(NETSOCKET sock, int backlog)
{
	int err = -1;
	if(sock.ipv4sock >= 0)
		err = listen(sock.ipv4sock, backlog);
	if(sock.ipv6sock >= 0)
		err = listen(sock.ipv6sock, backlog);
	return err;
}

int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *a)
{
	int s;
	socklen_t sockaddr_len;

	*new_sock = invalid_socket;

	if(sock.ipv4sock >= 0)
	{
		struct sockaddr_in addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock.ipv4sock, (struct sockaddr *)&addr, &sockaddr_len);

		if(s != -1)
		{
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);
			new_sock->type = NETTYPE_IPV4;
			new_sock->ipv4sock = s;
			return s;
		}
	}

	if(sock.ipv6sock >= 0)
	{
		struct sockaddr_in6 addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock.ipv6sock, (struct sockaddr *)&addr, &sockaddr_len);

		if(s != -1)
		{
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);
			new_sock->type = NETTYPE_IPV6;
			new_sock->ipv6sock = s;
			return s;
		}
	}

	return -1;
}

int net_tcp_connect(NETSOCKET sock, const NETADDR *a)
{
	if(a->type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		netaddr_to_sockaddr_in(a, &addr);
		return connect(sock.ipv4sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	if(a->type & NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		netaddr_to_sockaddr_in6(a, &addr);
		return connect(sock.ipv6sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	return -1;
}

int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr)
{
	int res = 0;

	net_set_non_blocking(sock);
	res = net_tcp_connect(sock, &bindaddr);
	net_set_blocking(sock);

	return res;
}

int net_tcp_send(NETSOCKET sock, const void *data, int size)
{
	int bytes = -1;

	if(sock.ipv4sock >= 0)
		bytes = send((int)sock.ipv4sock, (const char *)data, size, 0);
	if(sock.ipv6sock >= 0)
		bytes = send((int)sock.ipv6sock, (const char *)data, size, 0);

	return bytes;
}

int net_tcp_recv(NETSOCKET sock, void *data, int maxsize)
{
	int bytes = -1;

	if(sock.ipv4sock >= 0)
		bytes = recv((int)sock.ipv4sock, (char *)data, maxsize, 0);
	if(sock.ipv6sock >= 0)
		bytes = recv((int)sock.ipv6sock, (char *)data, maxsize, 0);

	return bytes;
}

int net_tcp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

int net_errno()
{
#if defined(CONF_FAMILY_WINDOWS)
	return WSAGetLastError();
#else
	return errno;
#endif
}

int net_would_block()
{
#if defined(CONF_FAMILY_WINDOWS)
	return net_errno() == WSAEWOULDBLOCK;
#else
	return net_errno() == EWOULDBLOCK;
#endif
}

int net_init()
{
#if defined(CONF_FAMILY_WINDOWS)
	WSADATA wsaData;
	int err = WSAStartup(MAKEWORD(1, 1), &wsaData);
	dbg_assert(err == 0, "network initialization failed.");
	return err == 0 ? 0 : 1;
#endif

	return 0;
}

int fs_listdir_info(const char *dir, FS_LISTDIR_INFO_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024 * 2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if(handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer + length, finddata.cFileName, (int)sizeof(buffer) - length);
		if(cb(finddata.cFileName, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	} while(FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024 * 2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer + length, entry->d_name, (int)sizeof(buffer) - length);
		if(cb(entry->d_name, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024 * 2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if(handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer + length, finddata.cFileName, (int)sizeof(buffer) - length);
		if(cb(finddata.cFileName, fs_is_dir(buffer), type, user))
			break;
	} while(FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024 * 2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer + length, entry->d_name, (int)sizeof(buffer) - length);
		if(cb(entry->d_name, fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_storage_path(const char *appname, char *path, int max)
{
#if defined(CONF_FAMILY_WINDOWS)
	char *home = getenv("APPDATA");
	if(!home)
		return -1;
	_snprintf(path, max, "%s/%s", home, appname);
	return 0;
#else
	char *home = getenv("HOME");
#if !defined(CONF_PLATFORM_MACOSX)
	int i;
#endif
	if(!home)
		return -1;

#if defined(CONF_PLATFORM_MACOSX)
	snprintf(path, max, "%s/Library/Application Support/%s", home, appname);
#else
	snprintf(path, max, "%s/.%s", home, appname);
	for(i = str_length(home) + 2; path[i]; i++)
		path[i] = tolower((unsigned char)path[i]);
#endif

	return 0;
#endif
}

int fs_makedir_rec_for(const char *path)
{
	char buffer[1024 * 2];
	char *p;
	str_copy(buffer, path, sizeof(buffer));
	for(p = buffer + 1; *p != '\0'; p++)
	{
		if(*p == '/' && *(p + 1) != '\0')
		{
			*p = '\0';
			if(fs_makedir(buffer) < 0)
				return -1;
			*p = '/';
		}
	}
	return 0;
}

int fs_makedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(_mkdir(path) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#else
	if(mkdir(path, 0755) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#endif
}

int fs_removedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(_rmdir(path) == 0)
		return 0;
	return -1;
#else
	if(rmdir(path) == 0)
		return 0;
	return -1;
#endif
}

int fs_is_dir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	/* TODO: do this smarter */
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024 * 2];
	str_format(buffer, sizeof(buffer), "%s/*", path);

	if((handle = FindFirstFileA(buffer, &finddata)) == INVALID_HANDLE_VALUE)
		return 0;

	FindClose(handle);
	return 1;
#else
	struct stat sb;
	if(stat(path, &sb) == -1)
		return 0;

	if(S_ISDIR(sb.st_mode))
		return 1;
	else
		return 0;
#endif
}

time_t fs_getmtime(const char *path)
{
	struct stat sb;
	if(stat(path, &sb) == -1)
		return 0;

	return sb.st_mtime;
}

int fs_chdir(const char *path)
{
	if(fs_is_dir(path))
	{
		if(chdir(path))
			return 1;
		else
			return 0;
	}
	else
		return 1;
}

char *fs_getcwd(char *buffer, int buffer_size)
{
	if(buffer == 0)
		return 0;
#if defined(CONF_FAMILY_WINDOWS)
	return _getcwd(buffer, buffer_size);
#else
	return getcwd(buffer, buffer_size);
#endif
}

int fs_parent_dir(char *path)
{
	char *parent = 0;
	for(; *path; ++path)
	{
		if(*path == '/' || *path == '\\')
			parent = path;
	}

	if(parent)
	{
		*parent = 0;
		return 0;
	}
	return 1;
}

int fs_remove(const char *filename)
{
	if(remove(filename) != 0)
		return 1;
	return 0;
}

int fs_rename(const char *oldname, const char *newname)
{
	if(rename(oldname, newname) != 0)
		return 1;
	return 0;
}

void swap_endian(void *data, unsigned elem_size, unsigned num)
{
	char *src = (char *)data;
	char *dst = src + (elem_size - 1);

	while(num)
	{
		unsigned n = elem_size >> 1;
		char tmp;
		while(n)
		{
			tmp = *src;
			*src = *dst;
			*dst = tmp;

			src++;
			dst--;
			n--;
		}

		src = src + (elem_size >> 1);
		dst = src + (elem_size - 1);
		num--;
	}
}

int net_socket_read_wait(NETSOCKET sock, int time)
{
	struct timeval tv;
	fd_set readfds;
	int sockid;

	tv.tv_sec = time / 1000;
	tv.tv_usec = 1000 * (time % 1000);
	sockid = 0;

	FD_ZERO(&readfds);
	if(sock.ipv4sock >= 0)
	{
		FD_SET(sock.ipv4sock, &readfds);
		sockid = sock.ipv4sock;
	}
	if(sock.ipv6sock >= 0)
	{
		FD_SET(sock.ipv6sock, &readfds);
		if(sock.ipv6sock > sockid)
			sockid = sock.ipv6sock;
	}

	/* don't care about writefds and exceptfds */
	if(time < 0)
		select(sockid + 1, &readfds, NULL, NULL, NULL);
	else
		select(sockid + 1, &readfds, NULL, NULL, &tv);

	if(sock.ipv4sock >= 0 && FD_ISSET(sock.ipv4sock, &readfds))
		return 1;

	if(sock.ipv6sock >= 0 && FD_ISSET(sock.ipv6sock, &readfds))
		return 1;

	return 0;
}

int time_timestamp()
{
	return time(0);
}

void str_append(char *dst, const char *src, int dst_size)
{
	int s = strlen(dst);
	int i = 0;
	while(s < dst_size)
	{
		dst[s] = src[i];
		if(!src[i]) /* check for null termination */
			break;
		s++;
		i++;
	}

	dst[dst_size-1] = 0; /* assure null termination */
}

static const char *str_token_get(const char *str, const char *delim, int *length)
{
	size_t len = strspn(str, delim);
	if(len > 1)
		str++;
	else
		str += len;
	if(!*str)
		return NULL;

	*length = strcspn(str, delim);
	return str;
}

int str_in_list(const char *list, const char *delim, const char *needle)
{
	const char *tok = list;
	int len = 0, notfound = 1, needlelen = str_length(needle);

	while(notfound && (tok = str_token_get(tok, delim, &len)))
	{
		notfound = needlelen != len || str_comp_num(tok, needle, len);
		tok = tok + len;
	}

	return !notfound;
}

//TeeUniverses
void str_append_num(char *dst, const char *src, int dst_size, int num)
{
	int s = strlen(dst);
	int i = 0;
	while(s < dst_size)
	{
		if(i >= num)
		{
			dst[s] = 0;
			return;
		}

		dst[s] = src[i];
		if(!src[i]) /* check for null termination */
			return;
		s++;
		i++;
	}

	dst[dst_size - 1] = 0; /* assure null termination */
}

void str_copy(char *dst, const char *src, int dst_size)
{
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = 0; /* assure null termination */
}

int str_length(const char *str)
{
	return (int)strlen(str);
}

int str_format(char *buffer, int buffer_size, const char *format, ...)
{
	int ret;
#if defined(CONF_FAMILY_WINDOWS)
	va_list ap;
	va_start(ap, format);
	ret = _vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);

	buffer[buffer_size - 1] = 0; /* assure null termination */

	/* _vsnprintf is documented to return negative values on truncation, but
	 * in practice we didn't see that. let's handle it anyway just in case. */
	if(ret < 0)
		ret = buffer_size - 1;
#else
	va_list ap;
	va_start(ap, format);
	ret = vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);

	/* null termination is assured by definition of vsnprintf */
#endif

	/* a return value of buffer_size or more indicates truncated output */
	if(ret >= buffer_size)
		ret = buffer_size - 1;

	return ret;
}



/* makes sure that the string only contains the characters between 32 and 127 */
void str_sanitize_strong(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		*str &= 0x7f;
		if(*str < 32)
			*str = 32;
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 */
void str_sanitize_cc(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32)
			*str = ' ';
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 + \r\n\t */
void str_sanitize(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32 && !(*str == '\r') && !(*str == '\n') && !(*str == '\t'))
			*str = ' ';
		str++;
	}
}

char *str_skip_to_whitespace(char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

char *str_skip_whitespaces(char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
		str++;
	return str;
}

/* case */
int str_comp_nocase(const char *a, const char *b)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _stricmp(a, b);
#else
	return strcasecmp(a, b);
#endif
}

int str_comp_nocase_num(const char *a, const char *b, const int num)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _strnicmp(a, b, num);
#else
	return strncasecmp(a, b, num);
#endif
}

int str_comp(const char *a, const char *b)
{
	return strcmp(a, b);
}

int str_comp_num(const char *a, const char *b, const int num)
{
	return strncmp(a, b, num);
}

int str_comp_filenames(const char *a, const char *b)
{
	int result;

	for(; *a && *b; ++a, ++b)
	{
		if(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			result = 0;
			do
			{
				if(!result)
					result = *a - *b;
				++a;
				++b;
			} while(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9');

			if(*a >= '0' && *a <= '9')
				return 1;
			else if(*b >= '0' && *b <= '9')
				return -1;
			else if(result)
				return result;
		}

		if(*a != *b)
			break;
	}
	return *a - *b;
}

const char *str_startswith(const char *str, const char *prefix)
{
	int prefixl = str_length(prefix);
	if(str_comp_num(str, prefix, prefixl) == 0)
	{
		return str + prefixl;
	}
	else
	{
		return 0;
	}
}

const char *str_endswith(const char *str, const char *suffix)
{
	int strl = str_length(str);
	int suffixl = str_length(suffix);
	const char *strsuffix;
	if(strl < suffixl)
	{
		return 0;
	}
	strsuffix = str + strl - suffixl;
	if(str_comp(strsuffix, suffix) == 0)
	{
		return strsuffix;
	}
	else
	{
		return 0;
	}
}

const char *str_find_nocase(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && tolower(*a) == tolower(*b))
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

const char *str_find(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && *a == *b)
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

void str_hex(char *dst, int dst_size, const void *data, int data_size)
{
	static const char hex[] = "0123456789ABCDEF";
	int b;

	for(b = 0; b < data_size && b < dst_size / 4 - 4; b++)
	{
		dst[b * 3] = hex[((const unsigned char *)data)[b] >> 4];
		dst[b * 3 + 1] = hex[((const unsigned char *)data)[b] & 0xf];
		dst[b * 3 + 2] = ' ';
		dst[b * 3 + 3] = 0;
	}
}

/* DDNET MODIFICATION START *******************************************/
void str_timestamp_ex(time_t time_data, char *buffer, int buffer_size, const char *format)
{
	struct tm *time_info;
	time_info = localtime(&time_data);
	strftime(buffer, buffer_size, format, time_info);
	buffer[buffer_size - 1] = 0; /* assure null termination */
}

void str_timestamp_format(char *buffer, int buffer_size, const char *format)
{
	time_t time_data;
	time(&time_data);
	str_timestamp_ex(time_data, buffer, buffer_size, format);
}

void str_timestamp(char *buffer, int buffer_size)
{
	str_timestamp_format(buffer, buffer_size, FORMAT_NOSPACE);
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

int str_time(int64 centisecs, int format, char *buffer, int buffer_size)
{
	const int sec = 100;
	const int min = 60 * sec;
	const int hour = 60 * min;
	const int day = 24 * hour;

	if(buffer_size <= 0)
		return -1;

	if(centisecs < 0)
		centisecs = 0;

	buffer[0] = 0;

	switch(format)
	{
	case TIME_DAYS:
		if(centisecs >= day)
			return str_format(buffer, buffer_size, "%lldd %02lld:%02lld:%02lld", centisecs / day,
				(centisecs % day) / hour, (centisecs % hour) / min, (centisecs % min) / sec);
		// fall through
	case TIME_HOURS:
		if(centisecs >= hour)
			return str_format(buffer, buffer_size, "%02lld:%02lld:%02lld", centisecs / hour,
				(centisecs % hour) / min, (centisecs % min) / sec);
		// fall through
	case TIME_MINS:
		return str_format(buffer, buffer_size, "%02lld:%02lld", centisecs / min,
			(centisecs % min) / sec);
	case TIME_HOURS_CENTISECS:
		if(centisecs >= hour)
			return str_format(buffer, buffer_size, "%02lld:%02lld:%02lld.%02lld", centisecs / hour,
				(centisecs % hour) / min, (centisecs % min) / sec, centisecs % sec);
		// fall through
	case TIME_MINS_CENTISECS:
		return str_format(buffer, buffer_size, "%02lld:%02lld.%02lld", centisecs / min,
			(centisecs % min) / sec, centisecs % sec);
	}

	return -1;
}

int str_time_float(float secs, int format, char *buffer, int buffer_size)
{
	return str_time(llroundf(secs * 100.0), format, buffer, buffer_size);
}

void str_escape(char **dst, const char *src, const char *end)
{
	while(*src && *dst < end)
	{
		if(*src == '"' || *src == '\\') // escape \ and "
			*(*dst)++ = '\\';
		*(*dst)++ = *src++;
	}
}

int mem_comp(const void *a, const void *b, int size)
{
	return memcmp(a, b, size);
}

void net_stats(NETSTATS *stats_inout)
{
	*stats_inout = network_stats;
}

void gui_messagebox(const char *title, const char *message)
{
#if defined(CONF_PLATFORM_MACOSX)
	DialogRef theItem;
	DialogItemIndex itemIndex;

	/* FIXME: really needed? can we rely on glfw? */
	/* HACK - get events without a bundle */
	ProcessSerialNumber psn;
	GetCurrentProcess(&psn);
	TransformProcessType(&psn,kProcessTransformToForegroundApplication);
	SetFrontProcess(&psn);
	/* END HACK */

	CreateStandardAlert(kAlertStopAlert,
			CFStringCreateWithCString(NULL, title, kCFStringEncodingASCII),
			CFStringCreateWithCString(NULL, message, kCFStringEncodingASCII),
			NULL,
			&theItem);

	RunStandardAlert(theItem, NULL, &itemIndex);
#elif defined(CONF_FAMILY_UNIX)
	static char cmd[1024];
	int err;
	/* use xmessage which is available on nearly every X11 system */
	snprintf(cmd, sizeof(cmd), "xmessage -center -title '%s' '%s'",
		title,
		message);

	err = system(cmd);
	dbg_msg("gui/msgbox", "result = %i", err);
#elif defined(CONF_FAMILY_WINDOWS)
	MessageBox(NULL,
		message,
		title,
		MB_ICONEXCLAMATION | MB_OK);
#else
	/* this is not critical */
	#warning not implemented
#endif
}

int str_isspace(char c) { return c == ' ' || c == '\n' || c == '\t'; }

char str_uppercase(char c)
{
	if(c >= 'a' && c <= 'z')
		return 'A' + (c - 'a');
	return c;
}

int str_toint(const char *str) { return atoi(str); }
float str_tofloat(const char *str) { return atof(str); }

const char *str_utf8_skip_whitespaces(const char *str)
{
	const char *str_old;
	int code;

	while(*str)
	{
		str_old = str;
		code = str_utf8_decode(&str);

		// check if unicode is not empty
		if(code > 0x20 && code != 0xA0 && code != 0x034F && (code < 0x2000 || code > 0x200F) && (code < 0x2028 || code > 0x202F) &&
			(code < 0x205F || code > 0x2064) && (code < 0x206A || code > 0x206F) && (code < 0xFE00 || code > 0xFE0F) &&
			code != 0xFEFF && (code < 0xFFF9 || code > 0xFFFC))
		{
			return str_old;
		}
	}

	return str;
}

int str_utf8_isstart(char c)
{
	if((c & 0xC0) == 0x80) /* 10xxxxxx */
		return 0;
	return 1;
}

int str_utf8_rewind(const char *str, int cursor)
{
	while(cursor)
	{
		cursor--;
		if(str_utf8_isstart(*(str + cursor)))
			break;
	}
	return cursor;
}

int str_utf8_forward(const char *str, int cursor)
{
	const char *buf = str + cursor;
	if(!buf[0])
		return cursor;

	if((*buf & 0x80) == 0x0) /* 0xxxxxxx */
		return cursor + 1;
	else if((*buf & 0xE0) == 0xC0) /* 110xxxxx */
	{
		if(!buf[1])
			return cursor + 1;
		return cursor + 2;
	}
	else if((*buf & 0xF0) == 0xE0) /* 1110xxxx */
	{
		if(!buf[1])
			return cursor + 1;
		if(!buf[2])
			return cursor + 2;
		return cursor + 3;
	}
	else if((*buf & 0xF8) == 0xF0) /* 11110xxx */
	{
		if(!buf[1])
			return cursor + 1;
		if(!buf[2])
			return cursor + 2;
		if(!buf[3])
			return cursor + 3;
		return cursor + 4;
	}

	/* invalid */
	return cursor + 1;
}

int str_utf8_encode(char *ptr, int chr)
{
	/* encode */
	if(chr <= 0x7F)
	{
		ptr[0] = (char)chr;
		return 1;
	}
	else if(chr <= 0x7FF)
	{
		ptr[0] = 0xC0 | ((chr >> 6) & 0x1F);
		ptr[1] = 0x80 | (chr & 0x3F);
		return 2;
	}
	else if(chr <= 0xFFFF)
	{
		ptr[0] = 0xE0 | ((chr >> 12) & 0x0F);
		ptr[1] = 0x80 | ((chr >> 6) & 0x3F);
		ptr[2] = 0x80 | (chr & 0x3F);
		return 3;
	}
	else if(chr <= 0x10FFFF)
	{
		ptr[0] = 0xF0 | ((chr >> 18) & 0x07);
		ptr[1] = 0x80 | ((chr >> 12) & 0x3F);
		ptr[2] = 0x80 | ((chr >> 6) & 0x3F);
		ptr[3] = 0x80 | (chr & 0x3F);
		return 4;
	}

	return 0;
}

int str_utf8_decode(const char **ptr)
{
	const char *buf = *ptr;
	int ch = 0;

	do
	{
		if((*buf&0x80) == 0x0)  /* 0xxxxxxx */
		{
			ch = *buf;
			buf++;
		}
		else if((*buf&0xE0) == 0xC0) /* 110xxxxx */
		{
			ch  = (*buf++ & 0x3F) << 6; if(!(*buf)) break;
			ch += (*buf++ & 0x3F);
			if(ch == 0) ch = -1;
		}
		else  if((*buf & 0xF0) == 0xE0)	/* 1110xxxx */
		{
			ch  = (*buf++ & 0x1F) << 12; if(!(*buf)) break;
			ch += (*buf++ & 0x3F) <<  6; if(!(*buf)) break;
			ch += (*buf++ & 0x3F);
			if(ch == 0) ch = -1;
		}
		else if((*buf & 0xF8) == 0xF0)	/* 11110xxx */
		{
			ch  = (*buf++ & 0x0F) << 18; if(!(*buf)) break;
			ch += (*buf++ & 0x3F) << 12; if(!(*buf)) break;
			ch += (*buf++ & 0x3F) <<  6; if(!(*buf)) break;
			ch += (*buf++ & 0x3F);
			if(ch == 0) ch = -1;
		}
		else
		{
			/* invalid */
			buf++;
			break;
		}

		*ptr = buf;
		return ch;
	} while(0);

	/* out of bounds */
	*ptr = buf;
	return -1;

}

int str_utf8_check(const char *str)
{
	while(*str)
	{
		if((*str&0x80) == 0x0)
			str++;
		else if((*str&0xE0) == 0xC0 && (*(str+1)&0xC0) == 0x80)
			str += 2;
		else if((*str&0xF0) == 0xE0 && (*(str+1)&0xC0) == 0x80 && (*(str+2)&0xC0) == 0x80)
			str += 3;
		else if((*str&0xF8) == 0xF0 && (*(str+1)&0xC0) == 0x80 && (*(str+2)&0xC0) == 0x80 && (*(str+3)&0xC0) == 0x80)
			str += 4;
		else
			return 0;
	}
	return 1;
}


unsigned str_quickhash(const char *str)
{
	unsigned hash = 5381;
	for(; *str; str++)
		hash = ((hash << 5) + hash) + (*str); /* hash * 33 + c */
	return hash;
}

struct SECURE_RANDOM_DATA
{
	int initialized;
#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV provider;
#else
	IOHANDLE urandom;
#endif
};

static struct SECURE_RANDOM_DATA secure_random_data = {0};

int secure_random_init()
{
	if(secure_random_data.initialized)
	{
		return 0;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(CryptAcquireContext(&secure_random_data.provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#else
	secure_random_data.urandom = io_open("/dev/urandom", IOFLAG_READ);
	if(secure_random_data.urandom)
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#endif
}

void secure_random_fill(void *bytes, unsigned length)
{
	if(!secure_random_data.initialized)
	{
		dbg_msg("secure", "called secure_random_fill before secure_random_init");
		dbg_break();
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(!CryptGenRandom(secure_random_data.provider, length, bytes))
	{
		dbg_msg("secure", "CryptGenRandom failed, last_error=%d", GetLastError());
		dbg_break();
	}
#else
	if(length != io_read(secure_random_data.urandom, bytes, length))
	{
		dbg_msg("secure", "io_read returned with a short read");
		dbg_break();
	}
#endif
}


#if defined(__cplusplus)
}
#endif
