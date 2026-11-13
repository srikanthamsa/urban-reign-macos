// rptmac - Steam Remote Play Together for any app on macOS
//
// Attaches to the running Steam client through steamclient.dylib and drives
// IClientRemoteClientManager, the same interface the Steam overlay uses for
// "Invite to Remote Play Together". Prints guest connect links.
//
// Usage:
//   rptmac apps [name-filter]
//   rptmac verify
//   rptmac host <appid> [game|desktop] [guests]
//   rptmac end <appid>
//
// GPL-3.0-or-later. The interface layout and the vtable indices are derived
// from the shipped Steam client; see tools/derive_indices.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <mach-o/dyld.h>

#define RPTMAC_VERSION "1.0.0"

typedef void* (*CreateInterfaceFn)(const char*, int*);

typedef struct {
    int32_t user;
    int32_t id;
    const void* param;
    int32_t size;
} CallbackMsg;

typedef struct {
    uint64_t steam_id;
    uint32_t guest_id;
    uint32_t group_id;
    uint64_t reserved[3];
} RemotePlayPlayer;

// IClientRemoteClientManager vtable layout for Steam client build 1794424036.
// Derived from the shipped steamclient.dylib (name xrefs inside each method).
enum {
    IDX_BHASINVITE         = 121,
    IDX_SETKB              = 132,
    IDX_SETMOUSE           = 133,
    IDX_SETCONTROLLER      = 134,
    IDX_CREATEGROUP        = 122,
    IDX_GETGROUP           = 123,
    IDX_GETGUEST           = 125,
    IDX_CREATEINVITE       = 126,
    IDX_SETDESKTOPRPT      = 130,
    IDX_DISBAND            = 142,
};

#define CB_INVITE_RESULT 1100028

static void* g_rcm;
static int g_pipe;
static int (*cb_get)(int, CallbackMsg*);
static void (*cb_free)(int);
static volatile sig_atomic_t g_stop;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void* vfn(void* obj, int i) { return ((void**)*(void***)obj)[i]; }
static uint32_t call_u32(int idx) {
    return ((uint32_t(*)(void*))vfn(g_rcm, idx))(g_rcm);
}
static uint32_t call_u32_u32(int idx, uint32_t a) {
    return ((uint32_t(*)(void*, uint32_t))vfn(g_rcm, idx))(g_rcm, a);
}
static void call_void_bool(int idx, int a) {
    ((void(*)(void*, uint8_t))vfn(g_rcm, idx))(g_rcm, (uint8_t)a);
}
static uint32_t call_invite(const RemotePlayPlayer* p, uint32_t appid) {
    return ((uint32_t(*)(void*, const RemotePlayPlayer*, uint32_t))vfn(g_rcm, IDX_CREATEINVITE))(g_rcm, p, appid);
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int extract_url(const uint8_t* p, int size, char* out) {
    static const char* keys[] = {"https://s.team/remoteplay/connect/", "steam://remoteplay"};
    for (int k = 0; k < 2; k++) {
        size_t kl = strlen(keys[k]);
        for (int i = 0; i + (int)kl < size; i++) {
            if (memcmp(p + i, keys[k], kl) != 0) continue;
            int n = 0;
            while (i + n < size && n < 1023 && p[i + n] >= 32 && p[i + n] < 127) {
                out[n] = (char)p[i + n];
                n++;
            }
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

// Steam's install location varies (home install, /Applications, legacy layout).
static const char* steamclient_path(void) {
    static char found[PATH_MAX];
    const char* env = getenv("STEAMCLIENT_DYLIB");
    if (env && *env) return env;
    const char* home = getenv("HOME");
    static const char* rel[] = {
        "Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib",
        "Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/Resources/steamclient.dylib",
        "Applications/Steam.app/Contents/MacOS/steamclient.dylib",
    };
    if (home) {
        for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
            snprintf(found, sizeof(found), "%s/%s", home, rel[i]);
            if (access(found, R_OK) == 0) return found;
        }
    }
    static const char* abs[] = {
        "/Applications/Steam.app/Contents/MacOS/steamclient.dylib",
        "/Applications/Steam.app/Contents/Resources/steamclient.dylib",
    };
    for (size_t i = 0; i < sizeof(abs) / sizeof(abs[0]); i++) {
        if (access(abs[i], R_OK) == 0) {
            snprintf(found, sizeof(found), "%s", abs[i]);
            return found;
        }
    }
    return NULL;
}

static int attach(void) {
    const char* path = steamclient_path();
    if (!path) {
        fprintf(stderr, "Steam's steamclient.dylib was not found. Install Steam or set STEAMCLIENT_DYLIB.\n");
        return 0;
    }
    void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "cannot load %s: %s\n", path, dlerror());
        return 0;
    }
    CreateInterfaceFn ci = (CreateInterfaceFn)dlsym(h, "CreateInterface");
    cb_get = (int (*)(int, CallbackMsg*))dlsym(h, "Steam_BGetCallback");
    cb_free = (void (*)(int))dlsym(h, "Steam_FreeLastCallback");
    if (!ci || !cb_get || !cb_free) {
        fprintf(stderr, "steamclient.dylib is missing expected exports\n");
        return 0;
    }
    int rc = 0;
    void* client = ci("SteamClient019", &rc);
    if (!client) {
        fprintf(stderr, "SteamClient019 unavailable (rc=%d)\n", rc);
        return 0;
    }
    int (*create_pipe)(void*) = (int (*)(void*))vfn(client, 0);
    int (*connect_user)(void*, int) = (int (*)(void*, int))vfn(client, 2);
    g_pipe = create_pipe(client);
    if (!g_pipe) {
        fprintf(stderr, "Steam is not running. Start Steam and sign in.\n");
        return 0;
    }
    if (!connect_user(client, g_pipe)) {
        fprintf(stderr, "Nobody is signed in to Steam.\n");
        return 0;
    }
    void* engine = ci("CLIENTENGINE_INTERFACE_VERSION005", &rc);
    if (!engine) {
        fprintf(stderr, "CLIENTENGINE interface missing (rc=%d)\n", rc);
        return 0;
    }
    g_rcm = ((void* (*)(void*, int))vfn(engine, 56))(engine, g_pipe);
    if (!g_rcm) {
        fprintf(stderr, "Steam did not provide its Remote Play manager\n");
        return 0;
    }
    return 1;
}

// The interface is undocumented and its layout moves between Steam builds.
// Refuse to call into a build we have not mapped instead of guessing.
static int self_check(void) {
    uint32_t enabled = call_u32(78);            // BIsStreamingEnabled
    uint32_t guest = call_u32(IDX_GETGUEST);    // GetAvailableRemotePlayTogetherGuestID
    if (enabled > 1) {
        fprintf(stderr, "unsupported Steam build: BIsStreamingEnabled returned %u\n", enabled);
        return 0;
    }
    if (guest > 64) {
        fprintf(stderr, "unsupported Steam build: guest slot id %u looks wrong\n", guest);
        return 0;
    }
    return 1;
}

static uint32_t wait_group(uint32_t appid, double seconds) {
    double deadline = now_s() + seconds;
    while (now_s() < deadline) {
        uint32_t g = call_u32_u32(IDX_GETGROUP, appid);
        if (g) return g;
        usleep(150000);
    }
    return 0;
}

static int create_link(uint32_t appid, uint32_t group, char* url) {
    uint32_t guest = call_u32(IDX_GETGUEST);
    if (!guest) guest = 1;
    RemotePlayPlayer p;
    memset(&p, 0, sizeof(p));
    p.guest_id = guest;
    p.group_id = group;
    if (!call_invite(&p, appid)) {
        fprintf(stderr, "Steam refused to create an invite slot\n");
        return 0;
    }
    double deadline = now_s() + 20.0;
    while (now_s() < deadline) {
        CallbackMsg m = {0};
        if (!cb_get(g_pipe, &m)) {
            usleep(30000);
            continue;
        }
        if (m.id == CB_INVITE_RESULT && m.param && m.size > 0) {
            int ok = extract_url((const uint8_t*)m.param, m.size, url);
            cb_free(g_pipe);
            if (ok) return 1;
        } else {
            cb_free(g_pipe);
        }
    }
    fprintf(stderr, "Steam did not return an invite link inside 20s\n");
    return 0;
}

static void disband(void) {
    call_void_bool(IDX_SETDESKTOPRPT, 0);
    ((void (*)(void*))vfn(g_rcm, IDX_DISBAND))(g_rcm);
}

// Non-Steam shortcuts live in Steam's binary VDF. Reading it is the only way to
// learn the appid Steam assigned to a shortcut (it hashes the exe path and name).
static uint32_t g_entry_appid;
static char g_entry_name[256];
static char g_entry_exe[1024];
static int g_have_entry;
static const char* g_apps_filter;

static int rd_cstr(const uint8_t* d, size_t n, size_t* o, char* out, size_t cap) {
    size_t s = *o, e = s;
    while (e < n && d[e]) e++;
    if (e >= n) return 0;
    size_t len = e - s;
    if (len >= cap) len = cap - 1;
    memcpy(out, d + s, len);
    out[len] = 0;
    *o = e + 1;
    return 1;
}

static void print_entry(void) {
    if (!g_entry_appid || !g_entry_name[0]) return;
    if (g_apps_filter && g_apps_filter[0] &&
        !strcasestr(g_entry_name, g_apps_filter) && !strcasestr(g_entry_exe, g_apps_filter))
        return;
    printf("%u\t%s\t%s\n", g_entry_appid, g_entry_name, g_entry_exe);
}

static void walk_vdf(const uint8_t* d, size_t n, size_t* o, int depth) {
    char key[256], val[1024];
    while (*o < n) {
        uint8_t type = d[(*o)++];
        if (type == 0x08) return;              // end of this map
        if (!rd_cstr(d, n, o, key, sizeof(key))) return;
        if (type == 0x00) {
            if (depth == 1) {                  // a shortcut entry starts
                g_entry_appid = 0;
                g_entry_name[0] = 0;
                g_entry_exe[0] = 0;
                g_have_entry = 1;
            }
            walk_vdf(d, n, o, depth + 1);
            if (depth == 1 && g_have_entry) print_entry();
            continue;
        }
        if (type == 0x01) {
            if (!rd_cstr(d, n, o, val, sizeof(val))) return;
            if (depth == 2) {
                if (strcmp(key, "AppName") == 0) snprintf(g_entry_name, sizeof(g_entry_name), "%s", val);
                else if (strcmp(key, "Exe") == 0) snprintf(g_entry_exe, sizeof(g_entry_exe), "%s", val);
            }
        } else if (type == 0x02) {
            if (*o + 4 > n) return;
            uint32_t v = (uint32_t)d[*o] | (uint32_t)d[*o + 1] << 8 | (uint32_t)d[*o + 2] << 16 |
                         (uint32_t)d[*o + 3] << 24;
            *o += 4;
            if (depth == 2 && strcmp(key, "appid") == 0) g_entry_appid = v;
        } else if (type == 0x07) {
            if (*o + 8 > n) return;
            *o += 8;
        } else {
            return;
        }
    }
}

static int list_apps(const char* filter) {
    g_apps_filter = filter;
    const char* home = getenv("HOME");
    if (!home) return 1;
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/Library/Application Support/Steam/userdata", home);
    DIR* ud = opendir(root);
    if (!ud) {
        fprintf(stderr, "no Steam userdata at %s\n", root);
        return 1;
    }
    int printed = 0;
    struct dirent* e;
    while ((e = readdir(ud))) {
        if (e->d_name[0] == '.') continue;
        char vdf[PATH_MAX];
        snprintf(vdf, sizeof(vdf), "%s/%s/config/shortcuts.vdf", root, e->d_name);
        FILE* f = fopen(vdf, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0 || len > (1 << 26)) { fclose(f); continue; }
        uint8_t* data = malloc((size_t)len);
        if (data && fread(data, 1, (size_t)len, f) == (size_t)len) {
            size_t off = 0;
            walk_vdf(data, (size_t)len, &off, 0);
            printed = 1;
        }
        free(data);
        fclose(f);
    }
    closedir(ud);
    if (!printed) fprintf(stderr, "no non-Steam shortcuts found\n");
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* mode = argc > 1 ? argv[1] : "help";
    if (strcmp(mode, "version") == 0) {
        printf("rptmac %s\n", RPTMAC_VERSION);
        return 0;
    }
    if (strcmp(mode, "apps") == 0) return list_apps(argc > 2 ? argv[2] : NULL);
    if (strcmp(mode, "help") == 0 || strcmp(mode, "-h") == 0 || strcmp(mode, "--help") == 0) {
        fprintf(stderr,
                "usage:\n"
                "  rptmac apps [name-filter]                list non-Steam shortcuts (appid, name, exe)\n"
                "  rptmac verify\n"
                "  rptmac host <appid> [game|desktop] [guests]\n"
                "  rptmac end <appid>\n");
        return 64;
    }
    if (argc < 3 && strcmp(mode, "verify") != 0 && strcmp(mode, "dump") != 0) return 64;
    uint32_t appid = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0;
    if (!attach()) return 3;
    if (!self_check()) return 6;

    if (strcmp(mode, "verify") == 0) {
        printf("steam pipe %d, manager %p\n", g_pipe, g_rcm);
        printf("streaming enabled : %u\n", call_u32(78));
        printf("group for %-9u: %u\n", appid, appid ? call_u32_u32(IDX_GETGROUP, appid) : 0);
        printf("free guest slot   : %u\n", call_u32(IDX_GETGUEST));
        return 0;
    }

    if (strcmp(mode, "dump") == 0) {
        // Diagnostic: vtable slot -> function offset inside steamclient.dylib,
        // consumed by tools/derive_indices.py after a Steam update.
        Dl_info info;
        dladdr((void*)vfn(g_rcm, 0), &info);
        uintptr_t base = (uintptr_t)info.dli_fbase;
        printf("base 0x%lx\n", (unsigned long)base);
        for (int i = 0; i < 200; i++) {
            uintptr_t f = (uintptr_t)vfn(g_rcm, i);
            if (!f || f < base || f > base + 0x14000000ULL) printf("%d out\n", i);
            else printf("%d 0x%lx\n", i, (unsigned long)(f - base));
        }
        return 0;
    }

    if (strcmp(mode, "end") == 0) {
        uint32_t group = call_u32_u32(IDX_GETGROUP, appid);
        if (group) {
            disband();
            printf("asked Steam to disband group %u\n", group);
        } else {
            printf("no remote play group for %u\n", appid);
        }
        return 0;
    }

    if (strcmp(mode, "host") != 0) return 64;

    const char* kind = argc > 3 ? argv[3] : "desktop";
    int desktop = strcmp(kind, "game") != 0;
    int guests = argc > 4 ? atoi(argv[4]) : 1;
    if (guests < 1) guests = 1;
    if (guests > 4) guests = 4;

    if (desktop) call_void_bool(IDX_SETDESKTOPRPT, 1);
    uint32_t created = call_u32_u32(IDX_CREATEGROUP, appid);
    uint32_t group = wait_group(appid, 6.0);
    if (!group) {
        fprintf(stderr, "Steam did not create a Remote Play Together group (create=%u)\n", created & 0xff);
        return 4;
    }
    fprintf(stderr, "session group %u for app %u (%s stream)\n", group, appid, desktop ? "desktop" : "game window");

    char url[1024];
    uint32_t slots[8];
    int nslots = 0;
    for (int i = 0; i < guests; i++) {
        memset(url, 0, sizeof(url));
        if (!create_link(appid, group, url)) return 5;
        printf("%s\n", url);
        slots[nslots++] = call_u32(IDX_GETGUEST) - 1; // slot that was just handed out
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    double next_inputs = 0;
    while (!g_stop) {
        CallbackMsg m = {0};
        if (cb_get(g_pipe, &m)) cb_free(g_pipe);
        else usleep(50000);
        if (now_s() >= next_inputs) {
            next_inputs = now_s() + 3.0;
            for (int i = 0; i < nslots; i++) {
                RemotePlayPlayer p;
                memset(&p, 0, sizeof(p));
                p.guest_id = slots[i];
                p.group_id = group;
                ((void (*)(void*, const RemotePlayPlayer*, uint8_t))vfn(g_rcm, IDX_SETKB))(g_rcm, &p, 1);
                ((void (*)(void*, const RemotePlayPlayer*, uint8_t))vfn(g_rcm, IDX_SETMOUSE))(g_rcm, &p, 1);
                ((void (*)(void*, const RemotePlayPlayer*, uint8_t))vfn(g_rcm, IDX_SETCONTROLLER))(g_rcm, &p, 1);
            }
        }
    }
    disband();
    fprintf(stderr, "session ended\n");
    return 0;
}
