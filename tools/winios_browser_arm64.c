/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pure native AArch64 Win32 OAuth & Web Browser Bridge (`WinIOSBrowser.exe`).
 *
 * Registered as Windows 11's `WinIOSHTML` / `MSEdgeHTM` HTTP+HTTPS handler:
 *   1. Detects OAuth loopback redirect ports (`localhost:<PORT>` / `127.0.0.1:<PORT>`,
 *      including single- or double-URL-encoded `redirect_uri=...` query strings),
 *      spawns a background TCP callback relay (`10.0.2.15:<PORT> <-> 127.0.0.1:<PORT>`),
 *      and registers the port on `http://10.0.2.4/.henge/expose-port`.
 *   2. Sends `PUT http://10.0.2.4/.henge/open-url` with the target URL:
 *      - `200`/`204`: iOS presents native `SFSafariViewController` (`SafariSheet`),
 *        and when the loopback OAuth server responds `200`/`302`, the relay signals
 *        `PUT http://10.0.2.4/.henge/oauth-done` to auto-dismiss `SafariSheet`.
 *      - `409` (`OAuth: Edge` mode) or offline: launches Microsoft Edge (`msedge.exe`).
 */
#include <stdint.h>
#include <stddef.h>

typedef void *HANDLE;
typedef uint16_t WCHAR;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef uintptr_t SOCKET;

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_SOCKET       ((SOCKET)(~0ULL))
#define AF_INET              2
#define AF_INET6             23
#define SOCK_STREAM          1
#define IPPROTO_TCP          6
#define SOL_SOCKET           0xFFFF
#define SO_REUSEADDR         0x0004
#define SO_SNDTIMEO          0x1005
#define SO_RCVTIMEO          0x1006
#define SD_SEND              1
#define CP_UTF8              65001U
#define CREATE_NO_WINDOW     0x08000000U
#define FILE_APPEND_DATA     0x0004U
#define OPEN_ALWAYS          4U
#define FILE_ATTRIBUTE_NORMAL 0x0080U
#define FILE_SHARE_READ      0x0001U
#define FILE_SHARE_WRITE     0x0002U

/* Network byte order IPv4 constants on little-endian ARM64 */
#define IPV4_HENGE_SHARE     0x0402000AU /* 10.0.2.4  */
#define IPV4_GUEST_NIC       0x0F02000AU /* 10.0.2.15 */
#define IPV4_LOOPBACK        0x0100007FU /* 127.0.0.1 */

typedef struct {
    int16_t  sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
} SockAddrIn;

typedef struct {
    int16_t  sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t  sin6_addr[16];
    uint32_t sin6_scope_id;
} SockAddrIn6;

typedef struct {
    DWORD  cb;
    WCHAR *lpReserved;
    WCHAR *lpDesktop;
    WCHAR *lpTitle;
    DWORD  dwX;
    DWORD  dwY;
    DWORD  dwXSize;
    DWORD  dwYSize;
    DWORD  dwXCountChars;
    DWORD  dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    uint16_t wShowWindow;
    uint16_t cbReserved2;
    uint8_t *lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} StartupInfoW;

typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} ProcessInformation;

/* kernel32.dll */
__declspec(dllimport) WCHAR *GetCommandLineW(void);
__declspec(dllimport) DWORD  GetModuleFileNameW(HANDLE hModule, WCHAR *lpFilename, DWORD nSize);
__declspec(dllimport) DWORD  GetTempPathW(DWORD nBufferLength, WCHAR *lpBuffer);
__declspec(dllimport) DWORD  GetFileAttributesW(const WCHAR *lpFileName);
__declspec(dllimport) int    WideCharToMultiByte(uint32_t CodePage, DWORD dwFlags, const WCHAR *lpWideCharStr,
                                                 int cchWideChar, char *lpMultiByteStr, int cbMultiByte,
                                                 const char *lpDefaultChar, BOOL *lpUsedDefaultChar);
__declspec(dllimport) int    MultiByteToWideChar(uint32_t CodePage, DWORD dwFlags, const char *lpMultiByteStr,
                                                 int cbMultiByte, WCHAR *lpWideCharStr, int cchWideChar);
__declspec(dllimport) HANDLE CreateFileW(const WCHAR *lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                         void *lpSecurityAttributes, DWORD dwCreationDisposition,
                                         DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
__declspec(dllimport) BOOL   WriteFile(HANDLE hFile, const void *lpBuffer, DWORD nBytes,
                                       DWORD *lpWritten, void *lpOverlapped);
__declspec(dllimport) BOOL   CloseHandle(HANDLE hObject);
__declspec(dllimport) BOOL   CreateProcessW(const WCHAR *lpApplicationName, WCHAR *lpCommandLine,
                                            void *lpProcAttr, void *lpThreadAttr, BOOL bInheritHandles,
                                            DWORD dwCreationFlags, void *lpEnvironment,
                                            const WCHAR *lpCurrentDirectory, StartupInfoW *lpStartupInfo,
                                            ProcessInformation *lpProcessInformation);
__declspec(dllimport) HANDLE CreateThread(void *lpThreadAttributes, size_t dwStackSize,
                                          DWORD (*lpStartAddress)(void *), void *lpParameter,
                                          DWORD dwCreationFlags, DWORD *lpThreadId);
__declspec(dllimport) DWORD  WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles,
                                                    BOOL bWaitAll, DWORD dwMilliseconds);
__declspec(dllimport) void   Sleep(DWORD dwMilliseconds);
__declspec(dllimport) uint64_t GetTickCount64(void);
__declspec(dllimport) HANDLE GetProcessHeap(void);
__declspec(dllimport) void  *HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes);
__declspec(dllimport) BOOL   HeapFree(HANDLE hHeap, DWORD dwFlags, void *lpMem);
__declspec(dllimport) void   ExitProcess(DWORD uExitCode);

/* ws2_32.dll */
__declspec(dllimport) int    WSAStartup(uint16_t wVersionRequested, void *lpWSAData);
__declspec(dllimport) SOCKET socket(int af, int type, int protocol);
__declspec(dllimport) int    setsockopt(SOCKET s, int level, int optname, const void *optval, int optlen);
__declspec(dllimport) int    bind(SOCKET s, const SockAddrIn *name, int namelen);
__declspec(dllimport) int    listen(SOCKET s, int backlog);
__declspec(dllimport) SOCKET accept(SOCKET s, SockAddrIn *addr, int *addrlen);
__declspec(dllimport) int    connect(SOCKET s, const SockAddrIn *name, int namelen);
__declspec(dllimport) int    send(SOCKET s, const char *buf, int len, int flags);
__declspec(dllimport) int    recv(SOCKET s, char *buf, int len, int flags);
__declspec(dllimport) int    shutdown(SOCKET s, int how);
__declspec(dllimport) int    closesocket(SOCKET s);

static void mem_zero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static volatile uint64_t g_relay_finish_at = 0;

static char to_lower_c(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int str_starts_with_ci(const char *s, const char *prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (!s[i] || to_lower_c(s[i]) != to_lower_c(prefix[i])) return 0;
        i++;
    }
    return 1;
}

static int str_eq(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int mem_contains_ci(const char *hay, size_t hay_len, const char *needle) {
    size_t nlen = str_len(needle);
    if (nlen == 0 || hay_len < nlen) return 0;
    for (size_t i = 0; i + nlen <= hay_len; i++) {
        size_t j = 0;
        while (j < nlen && to_lower_c(hay[i + j]) == to_lower_c(needle[j])) j++;
        if (j == nlen) return 1;
    }
    return 0;
}

static uint16_t htons_u16(uint16_t v) {
    return (uint16_t)(((v >> 8) & 0xFFU) | ((v & 0xFFU) << 8));
}

static int u32_to_dec(uint32_t v, char *out) {
    char tmp[16];
    int n = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return 1;
    }
    while (v > 0 && n < 15) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

static uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10U + (uint32_t)(*s - '0');
        s++;
        any = 1;
    }
    return any ? v : 0;
}

static void log_line(const char *msg1, const char *msg2) {
    WCHAR path[520];
    DWORD n = GetTempPathW(500, path);
    if (n == 0 || n > 480) return;
    const WCHAR *fn = u"winios_browser.log";
    for (size_t i = 0; fn[i] && n < 518; i++) path[n++] = fn[i];
    path[n] = 0;

    HANDLE hf = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    if (msg1 && msg1[0]) WriteFile(hf, msg1, (DWORD)str_len(msg1), &wr, NULL);
    if (msg2 && msg2[0]) WriteFile(hf, msg2, (DWORD)str_len(msg2), &wr, NULL);
    WriteFile(hf, "\r\n", 2, &wr, NULL);
    CloseHandle(hf);
}

static int send_all(SOCKET s, const char *buf, int len) {
    int off = 0;
    while (off < len) {
        int n = send(s, buf + off, len - off, 0);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

/* Raw Winsock HTTP PUT to 10.0.2.4:80 (`/.henge/open-url`, `/.henge/expose-port`, `/.henge/oauth-done`) */
static int http_put_henge(const char *path, const char *body, DWORD timeout_ms) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;

    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    SockAddrIn sa;
    mem_zero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons_u16(80);
    sa.sin_addr = IPV4_HENGE_SHARE;

    if (connect(s, &sa, sizeof(sa)) != 0) {
        closesocket(s);
        return -1;
    }

    size_t body_len = str_len(body);
    char hdr[512];
    int p = 0;
    const char *p1 = "PUT ";
    for (int i = 0; p1[i]; i++) hdr[p++] = p1[i];
    for (int i = 0; path[i] && p < 360; i++) hdr[p++] = path[i];
    const char *p2 = " HTTP/1.1\r\nHost: 10.0.2.4\r\nContent-Length: ";
    for (int i = 0; p2[i]; i++) hdr[p++] = p2[i];
    char clen[16];
    int cn = u32_to_dec((uint32_t)body_len, clen);
    for (int i = 0; i < cn; i++) hdr[p++] = clen[i];
    const char *p3 = "\r\nConnection: close\r\n\r\n";
    for (int i = 0; p3[i]; i++) hdr[p++] = p3[i];

    if (send_all(s, hdr, p) != 0 || (body_len > 0 && send_all(s, body, (int)body_len) != 0)) {
        closesocket(s);
        return -1;
    }

    char resp[256];
    int rn = recv(s, resp, sizeof(resp) - 1, 0);
    closesocket(s);
    if (rn < 12) return -1;
    resp[rn] = 0;

    /* Parse "HTTP/1.x NNN" */
    int sp = 0;
    while (sp < rn && resp[sp] != ' ') sp++;
    while (sp < rn && resp[sp] == ' ') sp++;
    if (sp + 3 <= rn && resp[sp] >= '0' && resp[sp] <= '9') {
        return (resp[sp] - '0') * 100 + (resp[sp + 1] - '0') * 10 + (resp[sp + 2] - '0');
    }
    return -1;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void url_decode_once(const char *src, char *dst, size_t cap) {
    size_t si = 0, di = 0;
    while (src[si] && di + 1 < cap) {
        if (src[si] == '%' && hex_val(src[si + 1]) >= 0 && hex_val(src[si + 2]) >= 0) {
            dst[di++] = (char)((hex_val(src[si + 1]) << 4) | hex_val(src[si + 2]));
            si += 3;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = 0;
}

static void launch_edge(const char *url_utf8) {
    const WCHAR *edge86 = u"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
    const WCHAR *edge64 = u"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe";
    const WCHAR *edge = (GetFileAttributesW(edge86) != 0xFFFFFFFFU) ? edge86 : edge64;
    if (GetFileAttributesW(edge) == 0xFFFFFFFFU) return;

    WCHAR *cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), 8 /*HEAP_ZERO_MEMORY*/, 8192 * sizeof(WCHAR));
    if (!cmd) return;

    size_t p = 0;
    cmd[p++] = '"';
    for (size_t i = 0; edge[i]; i++) cmd[p++] = edge[i];
    cmd[p++] = '"';
    cmd[p++] = ' ';
    cmd[p++] = '"';

    WCHAR *wurl = (WCHAR *)HeapAlloc(GetProcessHeap(), 8, 4096 * sizeof(WCHAR));
    if (wurl) {
        MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1, wurl, 4095);
        for (size_t i = 0; wurl[i] && p < 8180; i++) {
            if (wurl[i] == '"') {
                cmd[p++] = '%'; cmd[p++] = '2'; cmd[p++] = '2';
            } else {
                cmd[p++] = wurl[i];
            }
        }
        HeapFree(GetProcessHeap(), 0, wurl);
    }
    cmd[p++] = '"';
    cmd[p] = 0;

    StartupInfoW si;
    ProcessInformation pi;
    mem_zero(&si, sizeof(si));
    mem_zero(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (CreateProcessW(edge, cmd, NULL, NULL, 0, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    HeapFree(GetProcessHeap(), 0, cmd);
}

static void spawn_relay_process(uint16_t port) {
    WCHAR *self = (WCHAR *)HeapAlloc(GetProcessHeap(), 8, 1024 * sizeof(WCHAR));
    WCHAR *cmd  = (WCHAR *)HeapAlloc(GetProcessHeap(), 8, 1200 * sizeof(WCHAR));
    if (!self || !cmd) return;

    DWORD n = GetModuleFileNameW(NULL, self, 1020);
    if (n > 0) {
        size_t p = 0;
        cmd[p++] = '"';
        for (size_t i = 0; i < n; i++) cmd[p++] = self[i];
        const WCHAR * mid = u"\" --relay ";
        for (size_t i = 0; mid[i]; i++) cmd[p++] = mid[i];
        char pstr[16];
        int pn = u32_to_dec(port, pstr);
        for (int i = 0; i < pn; i++) cmd[p++] = (WCHAR)pstr[i];
        cmd[p] = 0;

        StartupInfoW si;
        ProcessInformation pi;
        mem_zero(&si, sizeof(si));
        mem_zero(&pi, sizeof(pi));
        si.cb = sizeof(si);
        if (CreateProcessW(self, cmd, NULL, NULL, 0, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            Sleep(80);
        }
    }
    HeapFree(GetProcessHeap(), 0, self);
    HeapFree(GetProcessHeap(), 0, cmd);
}

static void scan_and_expose_ports(const char *s, uint16_t *seen, int *seen_cnt) {
    size_t len = str_len(s);
    const char *prefixes[2] = { "localhost:", "127.0.0.1:" };
    for (int k = 0; k < 2; k++) {
        const char *pfx = prefixes[k];
        size_t plen = str_len(pfx);
        for (size_t i = 0; i + plen < len; i++) {
            if (str_starts_with_ci(s + i, pfx)) {
                const char * d = s + i + plen;
                uint32_t port = 0;
                int digits = 0;
                while (d[digits] >= '0' && d[digits] <= '9' && digits < 6) {
                    port = port * 10U + (uint32_t)(d[digits] - '0');
                    digits++;
                }
                if (digits >= 4 && port >= 1024U && port <= 65535U) {
                    uint16_t p16 = (uint16_t)port;
                    int dup = 0;
                    for (int m = 0; m < *seen_cnt; m++) {
                        if (seen[m] == p16) { dup = 1; break; }
                    }
                    if (!dup && *seen_cnt < 8) {
                        seen[(*seen_cnt)++] = p16;
                        char pstr[16];
                        u32_to_dec(p16, pstr);
                        log_line("expose_port=", pstr);
                        spawn_relay_process(p16);
                        http_put_henge("/.henge/expose-port", pstr, 1200);
                    }
                }
            }
        }
    }
}

/* ---- Background OAuth Callback TCP Relay (`10.0.2.15:<PORT> <-> 127.0.0.1:<PORT>`) ---- */

typedef struct {
    SOCKET src;
    SOCKET dst;
    volatile int *is_oauth;
    volatile int *done_signal;
    int inspect_request;
} PumpCtx;

static DWORD oauth_done_notify_thread(void *arg) {
    (void)arg;
    Sleep(550);
    http_put_henge("/.henge/oauth-done", "1", 2000);
    return 0;
}

static DWORD pump_stream_thread(void *arg) {
    PumpCtx *ctx = (PumpCtx *)arg;
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, 16384);
    if (!buf) return 0;

    int first = 1;
    for (;;) {
        int n = recv(ctx->src, buf, 16384, 0);
        if (n <= 0) break;
        if (first) {
            first = 0;
            size_t line_len = 0;
            while (line_len < (size_t)n && line_len < 1024 && buf[line_len] != '\r' && buf[line_len] != '\n') {
                line_len++;
            }
            if (ctx->inspect_request) {
                if (mem_contains_ci(buf, line_len, "/auth") ||
                    mem_contains_ci(buf, line_len, "/oauth") ||
                    mem_contains_ci(buf, line_len, "callback") ||
                    mem_contains_ci(buf, line_len, "code=")) {
                    *(ctx->is_oauth) = 1;
                }
            } else if (*(ctx->is_oauth)) {
                if (mem_contains_ci(buf, line_len, " 200 ") ||
                    mem_contains_ci(buf, line_len, " 302 ") ||
                    mem_contains_ci(buf, line_len, " 301 ")) {
                    *(ctx->done_signal) = 1;
                }
            }
        }
        if (send_all(ctx->dst, buf, n) != 0) break;
    }
    shutdown(ctx->dst, SD_SEND);
    HeapFree(GetProcessHeap(), 0, buf);
    return 0;
}

typedef struct {
    SOCKET client;
    uint16_t port;
} RelayClientArg;

static SOCKET connect_loopback_upstream(uint16_t port, DWORD io_tmo) {
    DWORD conn_tmo = 2000;
    for (int attempt = 0; attempt < 2; attempt++) {
        /* 1. Try IPv4 loopback 127.0.0.1:<port> */
        SOCKET s4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s4 != INVALID_SOCKET) {
            setsockopt(s4, SOL_SOCKET, SO_SNDTIMEO, &conn_tmo, sizeof(conn_tmo));
            setsockopt(s4, SOL_SOCKET, SO_RCVTIMEO, &io_tmo, sizeof(io_tmo));
            SockAddrIn sa4;
            mem_zero(&sa4, sizeof(sa4));
            sa4.sin_family = AF_INET;
            sa4.sin_port = htons_u16(port);
            sa4.sin_addr = IPV4_LOOPBACK;
            if (connect(s4, &sa4, sizeof(sa4)) == 0) {
                setsockopt(s4, SOL_SOCKET, SO_SNDTIMEO, &io_tmo, sizeof(io_tmo));
                return s4;
            }
            closesocket(s4);
        }

        /* 2. Try IPv6 loopback [::1]:<port> (Go/Node `localhost` on Windows IPV6_V6ONLY=1) */
        SOCKET s6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (s6 != INVALID_SOCKET) {
            setsockopt(s6, SOL_SOCKET, SO_SNDTIMEO, &conn_tmo, sizeof(conn_tmo));
            setsockopt(s6, SOL_SOCKET, SO_RCVTIMEO, &io_tmo, sizeof(io_tmo));
            SockAddrIn6 sa6;
            mem_zero(&sa6, sizeof(sa6));
            sa6.sin6_family = AF_INET6;
            sa6.sin6_port = htons_u16(port);
            sa6.sin6_addr[15] = 1;
            if (connect(s6, (const SockAddrIn *)&sa6, sizeof(sa6)) == 0) {
                setsockopt(s6, SOL_SOCKET, SO_SNDTIMEO, &io_tmo, sizeof(io_tmo));
                return s6;
            }
            closesocket(s6);
        }

        if (attempt == 0) Sleep(150);
    }
    return INVALID_SOCKET;
}

static DWORD relay_client_thread(void *arg) {
    RelayClientArg *rc = (RelayClientArg *)arg;
    SOCKET client = rc->client;
    uint16_t port = rc->port;
    HeapFree(GetProcessHeap(), 0, rc);

    DWORD io_tmo = 30000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io_tmo, sizeof(io_tmo));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io_tmo, sizeof(io_tmo));

    SOCKET upstream = connect_loopback_upstream(port, io_tmo);
    if (upstream == INVALID_SOCKET) {
        log_line("relay_upstream_connect_failed", "");
        closesocket(client);
        return 0;
    }

    volatile int is_oauth = 0;
    volatile int done_signal = 0;
    PumpCtx c2u = { client, upstream, &is_oauth, &done_signal, 1 };
    PumpCtx u2c = { upstream, client, &is_oauth, &done_signal, 0 };

    HANDLE th[2];
    th[0] = CreateThread(NULL, 0, pump_stream_thread, &c2u, 0, NULL);
    th[1] = CreateThread(NULL, 0, pump_stream_thread, &u2c, 0, NULL);
    if (th[0] && th[1]) {
        WaitForMultipleObjects(2, th, 1, 60000);
    }
    if (th[0]) CloseHandle(th[0]);
    if (th[1]) CloseHandle(th[1]);

    closesocket(upstream);
    closesocket(client);

    if (done_signal) {
        log_line("oauth_callback_done_signaling_host", "");
        g_relay_finish_at = GetTickCount64() + 4000ULL;
        HANDLE nt = CreateThread(NULL, 0, oauth_done_notify_thread, NULL, 0, NULL);
        if (nt) {
            WaitForMultipleObjects(1, &nt, 1, 3000);
            CloseHandle(nt);
        }
    }
    return 0;
}

static void run_relay_server(uint16_t port) {
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) return;

    SockAddrIn sa;
    mem_zero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons_u16(port);
    sa.sin_addr = IPV4_GUEST_NIC;

    if (bind(srv, &sa, sizeof(sa)) != 0 || listen(srv, 32) != 0) {
        closesocket(srv);
        return;
    }

    char pstr[16];
    u32_to_dec(port, pstr);
    log_line("relay_listening_10.0.2.15:", pstr);

    DWORD accept_tmo = 2000;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &accept_tmo, sizeof(accept_tmo));

    uint64_t deadline = GetTickCount64() + 30ULL * 60ULL * 1000ULL;
    while (GetTickCount64() < deadline) {
        uint64_t fin = g_relay_finish_at;
        if (fin != 0 && GetTickCount64() >= fin) break;
        SOCKET cl = accept(srv, NULL, NULL);
        if (cl == INVALID_SOCKET) {
            Sleep(40);
            continue;
        }
        deadline = GetTickCount64() + 30ULL * 60ULL * 1000ULL;
        RelayClientArg *rc = (RelayClientArg *)HeapAlloc(GetProcessHeap(), 0, sizeof(RelayClientArg));
        if (!rc) {
            closesocket(cl);
            continue;
        }
        rc->client = cl;
        rc->port = port;
        HANDLE th = CreateThread(NULL, 0, relay_client_thread, rc, 0, NULL);
        if (th) CloseHandle(th);
        else {
            HeapFree(GetProcessHeap(), 0, rc);
            closesocket(cl);
        }
    }
    closesocket(srv);
}

/* Parse UTF-8 command line into up to 16 arguments */
static int split_cmdline(char *cmdline, char **argv, int max_args) {
    int argc = 0;
    char *p = cmdline;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = 0;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
        }
    }
    return argc;
}

void WinMainCRTStartup(void) {
    uint8_t *wsa = (uint8_t *)HeapAlloc(GetProcessHeap(), 8, 512);
    if (wsa) WSAStartup(0x0202, wsa);

    WCHAR *wcmd = GetCommandLineW();
    char *utf8 = (char *)HeapAlloc(GetProcessHeap(), 8, 16384);
    if (!wcmd || !utf8) ExitProcess(0);

    WideCharToMultiByte(CP_UTF8, 0, wcmd, -1, utf8, 16380, NULL, NULL);
    log_line("invoked: ", utf8);

    char *argv[16];
    int argc = split_cmdline(utf8, argv, 16);

    if (argc >= 3 && str_eq(argv[1], "--relay")) {
        uint32_t p = parse_u32(argv[2]);
        if (p >= 1024U && p <= 65535U) {
            run_relay_server((uint16_t)p);
        }
        ExitProcess(0);
    }

    const char *target = NULL;
    for (int i = argc - 1; i >= 1; i--) {
        if (str_starts_with_ci(argv[i], "http://") || str_starts_with_ci(argv[i], "https://")) {
            target = argv[i];
            break;
        }
    }
    if (!target) {
        target = (argc >= 2) ? argv[argc - 1] : "https://www.google.com";
    }

    if (!str_starts_with_ci(target, "http://") && !str_starts_with_ci(target, "https://")) {
        launch_edge(target);
        ExitProcess(0);
    }

    char *dec1 = (char *)HeapAlloc(GetProcessHeap(), 8, 16384);
    char *dec2 = (char *)HeapAlloc(GetProcessHeap(), 8, 16384);
    uint16_t seen_ports[8];
    int seen_cnt = 0;

    scan_and_expose_ports(target, seen_ports, &seen_cnt);
    if (dec1) {
        url_decode_once(target, dec1, 16380);
        scan_and_expose_ports(dec1, seen_ports, &seen_cnt);
        if (dec2) {
            url_decode_once(dec1, dec2, 16380);
            scan_and_expose_ports(dec2, seen_ports, &seen_cnt);
        }
    }

    int status = http_put_henge("/.henge/open-url", target, 1500);
    char st_str[16];
    if (status >= 0) u32_to_dec((uint32_t)status, st_str);
    else { st_str[0] = '-'; st_str[1] = '1'; st_str[2] = 0; }
    log_line("open_url_status=", st_str);

    if (status == 200 || status == 204) {
        ExitProcess(0);
    }

    launch_edge(target);
    ExitProcess(0);
}
