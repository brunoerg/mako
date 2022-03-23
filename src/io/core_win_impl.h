/*!
 * core_win_impl.h - win32 environment for mako
 * Copyright (c) 2021-2022, Christopher Jeffrey (MIT License).
 * https://github.com/chjj/mako
 */

#define BTC_NEED_WINDOWS_H

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <shlobj.h>
#include <io/core.h>

/*
 * Types
 */

typedef struct btc_wide_s {
  WCHAR scratch[256];
  WCHAR *data;
} btc_wide_t;

/*
 * Encoding Helpers
 */

static int
btc_utf8_write(char *zp, size_t zn, const WCHAR *xp) {
  return WideCharToMultiByte(CP_UTF8, 0, xp, -1, zp, zn, NULL, NULL) != 0;
}

static size_t
btc_utf16_size(const char *xp) {
  return MultiByteToWideChar(CP_UTF8, 0, xp, -1, NULL, 0);
}

static int
btc_utf16_write(WCHAR *zp, size_t zn, const char *xp) {
  return MultiByteToWideChar(CP_UTF8, 0, xp, -1, zp, zn) != 0;
}

/*
 * Wide String
 */

static void
btc_wide_init(btc_wide_t *z, size_t zn) {
  if (zn > sizeof(z->scratch) / sizeof(WCHAR)) {
    z->data = malloc(zn * sizeof(WCHAR));

    if (z->data == NULL)
      abort(); /* LCOV_EXCL_LINE */
  } else {
    z->data = z->scratch;
  }
}

static void
btc_wide_clear(btc_wide_t *z) {
  if (z->data != z->scratch)
    free(z->data);
}

static int
btc_wide_import(btc_wide_t *z, const char *xp) {
  size_t zn = btc_utf16_size(xp);

  if (zn == 0)
    return 0;

  btc_wide_init(z, zn);

  if (!btc_utf16_write(z->data, zn, xp)) {
    btc_wide_clear(z);
    return 0;
  }

  return 1;
}

static int
btc_wide_export(char *zp, size_t zn, const btc_wide_t *x) {
  return btc_utf8_write(zp, zn, x->data);
}

/*
 * Compat
 */

static int
BTCIsWindowsNT(void) {
  /* Logic from libsodium/core.c */
  static volatile long state = 0;
  static DWORD version = 0;
  long value;

  while ((value = InterlockedCompareExchange(&state, 1, 0)) == 1)
    Sleep(0);

  if (value == 0) {
    version = GetVersion();

    if (InterlockedExchange(&state, 2) != 1)
      abort(); /* LCOV_EXCL_LINE */
  } else {
    assert(value == 2);
  }

  return version < 0x80000000;
}

static BOOL
BTCSetFilePointerEx(HANDLE file,
                    LARGE_INTEGER pos,
                    LARGE_INTEGER *rpos,
                    DWORD method) {
  pos.LowPart = SetFilePointer(file, pos.LowPart, &pos.HighPart, method);

  if (pos.LowPart == (DWORD)-1) { /* INVALID_SET_FILE_POINTER */
    if (GetLastError() != NO_ERROR)
      return FALSE;
  }

  if (rpos != NULL)
    *rpos = pos;

  return TRUE;
}

static BOOL
BTCGetFileSizeEx(HANDLE file, LARGE_INTEGER *size) {
  DWORD HighPart = 0;

  size->LowPart = GetFileSize(file, &HighPart);
  size->HighPart = HighPart;

  if (size->LowPart == (DWORD)-1) { /* INVALID_FILE_SIZE */
    if (GetLastError() != NO_ERROR)
      return FALSE;
  }

  return TRUE;
}

static HANDLE
BTCCreateFile(LPCSTR filename,
              DWORD access,
              DWORD share,
              LPSECURITY_ATTRIBUTES attrs,
              DWORD disposition,
              DWORD flags,
              HANDLE temp) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path;
    HANDLE handle;

    if (!btc_wide_import(&path, filename))
      return INVALID_HANDLE_VALUE;

    handle = CreateFileW(path.data,
                         access,
                         share,
                         attrs,
                         disposition,
                         flags,
                         temp);

    btc_wide_clear(&path);

    return handle;
  }

  return CreateFileA(filename,
                     access,
                     share,
                     attrs,
                     disposition,
                     flags,
                     temp);
}

/*
 * Filesystem
 */

btc_fd_t
btc_fs_open(const char *name) {
  return BTCCreateFile(name,
                       GENERIC_READ,
                       FILE_SHARE_READ,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

btc_fd_t
btc_fs_create(const char *name) {
  return BTCCreateFile(name,
                       GENERIC_WRITE,
                       0,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

btc_fd_t
btc_fs_append(const char *name) {
  HANDLE handle = BTCCreateFile(name,
                                GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);

  if (handle == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;

  if (SetFilePointer(handle, 0, NULL, FILE_END) == (DWORD)-1) {
    if (GetLastError() != NO_ERROR) {
      CloseHandle(handle);
      return INVALID_HANDLE_VALUE;
    }
  }

  return handle;
}

FILE *
btc_fs_fopen(const char *name, const char *mode) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path, flags;
    FILE *stream;

    if (!btc_wide_import(&path, name))
      return 0;

    if (!btc_wide_import(&flags, mode)) {
      btc_wide_clear(&path);
      return 0;
    }

    stream = _wfopen(path.data, flags.data);

    btc_wide_clear(&path);
    btc_wide_clear(&flags);

    return stream;
  }

  return fopen(name, mode);
}

int
btc_fs_close(btc_fd_t fd) {
  return CloseHandle(fd) != 0;
}

int
btc_fs_size(const char *name, uint64_t *size) {
  LARGE_INTEGER result;
  HANDLE handle;

  handle = BTCCreateFile(name,
                         0,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);

  if (handle == INVALID_HANDLE_VALUE)
    return 0;

  if (!BTCGetFileSizeEx(handle, &result)) {
    CloseHandle(handle);
    return 0;
  }

  CloseHandle(handle);

  *size = result.QuadPart;

  return 1;
}

int
btc_fs_exists(const char *name) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path;
    DWORD attrs;

    if (!btc_wide_import(&path, name))
      return 0;

    attrs = GetFileAttributesW(path.data);

    btc_wide_clear(&path);

    return attrs != INVALID_FILE_ATTRIBUTES;
  }

  return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
}

int
btc_fs_rename(const char *from, const char *to) {
  if (BTCIsWindowsNT()) {
    btc_wide_t src, dst;
    BOOL result;

    if (!btc_wide_import(&src, from))
      return 0;

    if (!btc_wide_import(&dst, to)) {
      btc_wide_clear(&src);
      return 0;
    }

    /* Windows NT only. */
    result = MoveFileExW(src.data, dst.data, MOVEFILE_REPLACE_EXISTING);

    btc_wide_clear(&src);
    btc_wide_clear(&dst);

    return result != 0;
  }

  /* Windows 9x fallback (non-atomic). */
  if (!MoveFileA(from, to)) {
    if (GetLastError() != ERROR_ALREADY_EXISTS)
      return 0;

    if (!DeleteFileA(to))
      return 0;

    if (!MoveFileA(from, to))
      return 0;
  }

  return 1;
}

int
btc_fs_unlink(const char *name) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path;
    BOOL result;

    if (!btc_wide_import(&path, name))
      return 0;

    result = DeleteFileW(path.data);

    btc_wide_clear(&path);

    return result != 0;
  }

  return DeleteFileA(name) != 0;
}

int
btc_fs_mkdir(const char *name) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path;
    BOOL result;

    if (!btc_wide_import(&path, name))
      return 0;

    result = CreateDirectoryW(path.data, NULL);

    btc_wide_clear(&path);

    return result != 0;
  }

  return CreateDirectoryA(name, NULL) != 0;
}

static int
btc_fs_mkdirp_wide(const char *name) {
  size_t len = btc_utf16_size(name);
  btc_wide_t tmp;
  WCHAR *path;
  size_t i;

  if (len == 0)
    return 0;

  btc_wide_init(&tmp, len);

  if (!btc_utf16_write(tmp.data, len, name))
    return 0;

  path = tmp.data;

  for (i = 0; i < len; i++) {
    if (name[i] == L'/')
      path[i] = L'\\';
    else
      path[i] = name[i];
  }

  i = 0;

  if ((path[0] >= L'A' && path[0] <= L'Z')
      || (path[0] >= L'a' && path[0] <= L'z')) {
    if (path[1] == L':' && path[2] == L'\0') {
      btc_wide_clear(&tmp);
      return 1;
    }

    if (path[1] == L':' && path[2] == L'\\')
      i += 3;
  }

  while (path[i] == L'\\')
    i += 1;

  for (; i < len; i++) {
    if (path[i] != L'\\' && path[i] != L'\0')
      continue;

    if (i > 0 && path[i - 1] == L'\\')
      continue;

    path[i] = L'\0';

    if (!CreateDirectoryW(path, NULL)) {
      if (GetLastError() != ERROR_ALREADY_EXISTS) {
        btc_wide_clear(&tmp);
        return 0;
      }
    }

    path[i] = L'\\';
  }

  btc_wide_clear(&tmp);

  return 1;
}

static int
btc_fs_mkdirp_ansi(const char *name) {
  size_t len = strlen(name);
  char path[MAX_PATH];
  size_t i;

  if (len + 1 > sizeof(path))
    return 0;

  for (i = 0; i < len + 1; i++) {
    if (name[i] == '/')
      path[i] = '\\';
    else
      path[i] = name[i];
  }

  i = 0;

  if ((path[0] >= 'A' && path[0] <= 'Z')
      || (path[0] >= 'a' && path[0] <= 'z')) {
    if (path[1] == ':' && path[2] == '\0')
      return 1;

    if (path[1] == ':' && path[2] == '\\')
      i += 3;
  }

  while (path[i] == '\\')
    i += 1;

  for (; i < len + 1; i++) {
    if (path[i] != '\\' && path[i] != '\0')
      continue;

    if (i > 0 && path[i - 1] == '\\')
      continue;

    path[i] = '\0';

    if (!CreateDirectoryA(path, NULL)) {
      if (GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
    }

    path[i] = '\\';
  }

  return 1;
}

int
btc_fs_mkdirp(const char *name) {
  if (BTCIsWindowsNT())
    return btc_fs_mkdirp_wide(name);

  return btc_fs_mkdirp_ansi(name);
}

int
btc_fs_rmdir(const char *name) {
  if (BTCIsWindowsNT()) {
    btc_wide_t path;
    BOOL result;

    if (!btc_wide_import(&path, name))
      return 0;

    result = RemoveDirectoryW(path.data);

    btc_wide_clear(&path);

    return result != 0;
  }

  return RemoveDirectoryA(name) != 0;
}

int
btc_fs_fsize(btc_fd_t fd, uint64_t *size) {
  LARGE_INTEGER result;

  if (!BTCGetFileSizeEx(fd, &result))
    return 0;

  *size = result.QuadPart;

  return 1;
}

int64_t
btc_fs_seek(btc_fd_t fd, int64_t pos) {
  LARGE_INTEGER dist, result;

  dist.QuadPart = pos;

  if (!BTCSetFilePointerEx(fd, dist, &result, FILE_BEGIN))
    return -1;

  return result.QuadPart;
}

int
btc_fs_read(btc_fd_t fd, void *dst, size_t len) {
  DWORD nread = 0;

  if (!ReadFile(fd, dst, len, &nread, NULL))
    return 0;

  if (nread != len)
    return 0;

  return 1;
}

int
btc_fs_write(btc_fd_t fd, const void *src, size_t len) {
  DWORD nwrite = 0;

  if (!WriteFile(fd, src, len, &nwrite, NULL))
    return 0;

  if (nwrite != len)
    return 0;

  return 1;
}

int
btc_fs_fsync(btc_fd_t fd) {
  return FlushFileBuffers(fd) != 0;
}

btc_fd_t
btc_fs_lock(const char *name) {
  HANDLE handle = BTCCreateFile(name,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ,
                                NULL,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);

  if (handle == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;

  if (!LockFile(handle, 0, 0, MAXDWORD, MAXDWORD)) {
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
  }

  return handle;
}

int
btc_fs_unlock(btc_fd_t fd) {
  BOOL result = UnlockFile(fd, 0, 0, MAXDWORD, MAXDWORD);

  CloseHandle(fd);

  return result != 0;
}

/*
 * Path
 */

int
btc_path_absolute(char *buf, size_t size, const char *name) {
  DWORD len;

  if (BTCIsWindowsNT()) {
    btc_wide_t path, tmp;
    int ret = 0;

    if (!btc_wide_import(&path, name))
      return 0;

    btc_wide_init(&tmp, size * 4);

    len = GetFullPathNameW(path.data, size * 4, tmp.data, NULL);

    if (len > 0 && len < size * 4)
      ret = btc_wide_export(buf, size, &tmp);

    btc_wide_clear(&path);
    btc_wide_clear(&tmp);

    return ret;
  }

  len = GetFullPathNameA(name, size, buf, NULL);

  return len > 0 && len < size;
}

/*
 * Process
 */

static void (*global_handler)(void *) = NULL;
static void *global_arg = NULL;
static int global_bound = 0;

int
btc_ps_daemon(void) {
  return 0;
}

int
btc_ps_fdlimit(int minfd) {
  return minfd < 2048 ? 2048 : minfd;
}

static BOOL WINAPI
real_handler(DWORD type) {
  /* Note: this runs on a separate thread. */
  /* May need to add a mutex for `loop->running`? */
  (void)type;

  if (global_handler != NULL) {
    global_handler(global_arg);
    global_handler = NULL;
  }

  Sleep(INFINITE); /* Prevent ExitProcess from being called. */

  return TRUE;
}

void
btc_ps_onterm(void (*handler)(void *), void *arg) {
  global_handler = handler;
  global_arg = arg;

  if (!global_bound) {
    global_bound = 1;

    SetConsoleCtrlHandler(real_handler, TRUE);
  }
}

size_t
btc_ps_rss(void) {
  /* GetProcessMemoryInfo is NT-only. */
  return 0;
}

/*
 * System
 */

int
btc_sys_numcpu(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors;
}

int
btc_sys_datadir(char *buf, size_t size, const char *name) {
  static BOOL (WINAPI *GetSpecialFolderPathW)(HWND, LPWSTR, int, BOOL) = NULL;
  static BOOL (WINAPI *GetSpecialFolderPathA)(HWND, LPSTR, int, BOOL) = NULL;
  static volatile long state = 0;
  char path[MAX_PATH * 4];
  long value;

  /* Logic from libsodium/core.c */
  while ((value = InterlockedCompareExchange(&state, 1, 0)) == 1)
    Sleep(0);

  if (value == 0) {
    HINSTANCE handle = LoadLibraryA("shell32.dll");

    if (handle != NULL) {
      *((FARPROC *)&GetSpecialFolderPathW) =
        GetProcAddress(handle, "SHGetSpecialFolderPathW");

      *((FARPROC *)&GetSpecialFolderPathA) =
        GetProcAddress(handle, "SHGetSpecialFolderPathA");
    }

    if (InterlockedExchange(&state, 2) != 1)
      abort(); /* LCOV_EXCL_LINE */
  } else {
    assert(value == 2);
  }

  if (BTCIsWindowsNT()) {
    WCHAR wpath[MAX_PATH];

    wpath[0] = 0;

    if (GetSpecialFolderPathW == NULL
        || !GetSpecialFolderPathW(NULL, wpath, CSIDL_APPDATA, FALSE)) {
      DWORD len = GetEnvironmentVariableW(L"USERPROFILE", wpath, MAX_PATH);

      if (len == 0 || len >= MAX_PATH)
        return 0;
    }

    if (!btc_utf8_write(path, sizeof(path), wpath))
      return 0;
  } else {
    path[0] = 0;

    if (GetSpecialFolderPathA == NULL
        || !GetSpecialFolderPathA(NULL, path, CSIDL_APPDATA, FALSE)) {
      DWORD len = GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH);

      if (len == 0 || len >= MAX_PATH)
        return 0;
    }
  }

  if (strlen(path) + strlen(name) + 2 > size)
    return 0;

  sprintf(buf, "%s\\%c%s", path, name[0] & ~32, name + 1);

  return 1;
}

/*
 * Time
 */

static double
btc_time_qpf(void) {
  /* Logic from libsodium/core.c */
  static volatile long state = 0;
  static double freq_inv = 0.0;
  LARGE_INTEGER freq;
  long value;

  while ((value = InterlockedCompareExchange(&state, 1, 0)) == 1)
    Sleep(0);

  if (value == 0) {
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0)
      freq_inv = 1.0 / (double)freq.QuadPart;

    if (InterlockedExchange(&state, 2) != 1)
      abort(); /* LCOV_EXCL_LINE */
  } else {
    assert(value == 2);
  }

  return freq_inv;
}

static int64_t
btc_time_qpc(double scale) {
  double freq_inv = btc_time_qpf();
  LARGE_INTEGER ctr;

  if (freq_inv == 0.0) {
    static const uint64_t epoch = UINT64_C(116444736000000000);
    ULARGE_INTEGER ul;
    uint64_t units;
    FILETIME ft;

    GetSystemTimeAsFileTime(&ft);

    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;

    units = ul.QuadPart - epoch;

    if (scale == 1.0)
      return units / 10000000;

    if (scale == 1000.0)
      return units / 10000;

    if (scale == 1000000.0)
      return units / 10;

    return units * 100;
  }

  if (!QueryPerformanceCounter(&ctr))
    abort(); /* LCOV_EXCL_LINE */

  return ((double)ctr.QuadPart * freq_inv) * scale;
}

int64_t
btc_time_sec(void) {
  return btc_time_qpc(1.0);
}

int64_t
btc_time_msec(void) {
  return btc_time_qpc(1000.0);
}

int64_t
btc_time_usec(void) {
  return btc_time_qpc(1000000.0);
}

void
btc_time_sleep(int64_t msec) {
  if (msec < 0)
    msec = 0;

  Sleep((DWORD)msec);
}

/*
 * Threads
 */

/*
 * Mutex
 */

static void
btc_mutex_tryinit(btc_mutex_t *mtx) {
  /* Logic from libsodium/core.c */
  long state;

  while ((state = InterlockedCompareExchange(&mtx->state, 1, 0)) == 1)
    Sleep(0);

  if (state == 0) {
    InitializeCriticalSection(&mtx->handle);

    if (InterlockedExchange(&mtx->state, 2) != 1)
      abort(); /* LCOV_EXCL_LINE */
  } else {
    assert(state == 2);
  }
}

void
btc_mutex_init(btc_mutex_t *mtx) {
  mtx->state = 2;
  InitializeCriticalSection(&mtx->handle);
}

void
btc_mutex_destroy(btc_mutex_t *mtx) {
  DeleteCriticalSection(&mtx->handle);
}

void
btc_mutex_lock(btc_mutex_t *mtx) {
  btc_mutex_tryinit(mtx);
  EnterCriticalSection(&mtx->handle);
}

void
btc_mutex_unlock(btc_mutex_t *mtx) {
  LeaveCriticalSection(&mtx->handle);
}

/*
 * Conditional
 */

void
btc_cond_init(btc_cond_t *cond) {
  cond->waiters = 0;

  InitializeCriticalSection(&cond->lock);

  cond->signal = CreateEventA(NULL, FALSE, FALSE, NULL);
  cond->broadcast = CreateEventA(NULL, TRUE, FALSE, NULL);

  if (!cond->signal || !cond->broadcast)
    abort(); /* LCOV_EXCL_LINE */
}

void
btc_cond_destroy(btc_cond_t *cond) {
  if (!CloseHandle(cond->broadcast))
    abort(); /* LCOV_EXCL_LINE */

  if (!CloseHandle(cond->signal))
    abort(); /* LCOV_EXCL_LINE */

  DeleteCriticalSection(&cond->lock);
}

void
btc_cond_signal(btc_cond_t *cond) {
  int have_waiters;

  EnterCriticalSection(&cond->lock);
  have_waiters = (cond->waiters > 0);
  LeaveCriticalSection(&cond->lock);

  if (have_waiters)
    SetEvent(cond->signal);
}

void
btc_cond_broadcast(btc_cond_t *cond) {
  int have_waiters;

  EnterCriticalSection(&cond->lock);
  have_waiters = (cond->waiters > 0);
  LeaveCriticalSection(&cond->lock);

  if (have_waiters)
    SetEvent(cond->broadcast);
}

void
btc_cond_wait(btc_cond_t *cond, btc_mutex_t *mtx) {
  HANDLE handles[2];
  int last_waiter;
  DWORD result;

  handles[0] = cond->signal;
  handles[1] = cond->broadcast;

  EnterCriticalSection(&cond->lock);
  cond->waiters++;
  LeaveCriticalSection(&cond->lock);

  LeaveCriticalSection(&mtx->handle);

  result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

  if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
    abort(); /* LCOV_EXCL_LINE */

  EnterCriticalSection(&cond->lock);
  cond->waiters--;
  last_waiter = (result == WAIT_OBJECT_0 + 1 && cond->waiters == 0);
  LeaveCriticalSection(&cond->lock);

  if (last_waiter)
    ResetEvent(cond->broadcast);

  EnterCriticalSection(&mtx->handle);
}

/*
 * Thread
 */

typedef struct btc_args_s {
  void (*start)(void *);
  void *arg;
} btc_args_t;

static DWORD WINAPI
btc_thread_run(void *ptr) {
  btc_args_t args = *((btc_args_t *)ptr);

  free(ptr);

  args.start(args.arg);

  return ERROR_SUCCESS;
}

void
btc_thread_create(btc_thread_t *thread, void (*start)(void *), void *arg) {
  btc_args_t *args = malloc(sizeof(btc_args_t));

  if (args == NULL)
    abort(); /* LCOV_EXCL_LINE */

  args->start = start;
  args->arg = arg;

  thread->handle = CreateThread(NULL, 0, btc_thread_run, args, 0, NULL);

  if (thread->handle == NULL)
    abort(); /* LCOV_EXCL_LINE */
}

void
btc_thread_detach(btc_thread_t *thread) {
  if (!CloseHandle(thread->handle))
    abort(); /* LCOV_EXCL_LINE */
}

void
btc_thread_join(btc_thread_t *thread) {
  WaitForSingleObject(thread->handle, INFINITE);

  if (!CloseHandle(thread->handle))
    abort(); /* LCOV_EXCL_LINE */
}