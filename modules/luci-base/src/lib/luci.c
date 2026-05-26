/*
 * LuCI low level routines - ucode binding
 *
 *   Copyright (C) 2009-2022 Jo-Philipp Wich <jo@mein.io>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "lmo.h"

#include <pwd.h>
#include <crypt.h>
#include <shadow.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

#include <ucode/module.h>

#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>


/* translation catalog functions */

static uc_value_t *
uc_luci_load_catalog(uc_vm_t *vm, size_t nargs) {
	uc_value_t *lang = uc_fn_arg(0);
	uc_value_t *dir  = uc_fn_arg(1);

	if (lang && ucv_type(lang) != UC_STRING)
		return NULL;

	if (dir && ucv_type(dir) != UC_STRING)
		return NULL;

	return ucv_boolean_new(lmo_load_catalog(
		lang ? ucv_string_get(lang) : "en",
		ucv_string_get(dir)) == 0);
}

static uc_value_t *
uc_luci_close_catalog(uc_vm_t *vm, size_t nargs) {
	uc_value_t *lang = uc_fn_arg(0);

	if (lang && ucv_type(lang) != UC_STRING)
		return NULL;

	lmo_close_catalog(lang ? ucv_string_get(lang) : "en");

	return ucv_boolean_new(true);
}

static uc_value_t *
uc_luci_change_catalog(uc_vm_t *vm, size_t nargs) {
	uc_value_t *lang = uc_fn_arg(0);

	if (lang && ucv_type(lang) != UC_STRING)
		return NULL;

	return ucv_boolean_new(lmo_change_catalog(
		lang ? ucv_string_get(lang) : "en") == 0);
}

static void
uc_luci_get_translations_cb(uint32_t key, const char *val, int len, void *priv) {
	uc_vm_t *vm = priv;

	uc_vm_stack_push(vm, ucv_get(uc_vm_stack_peek(vm, 0)));
	uc_vm_stack_push(vm, ucv_uint64_new(key));
	uc_vm_stack_push(vm, ucv_string_new_length(val, (size_t)len));

	if (uc_vm_call(vm, false, 2) == EXCEPTION_NONE)
		ucv_put(uc_vm_stack_pop(vm));
}

static uc_value_t *
uc_luci_get_translations(uc_vm_t *vm, size_t nargs) {
	lmo_iterate(uc_luci_get_translations_cb, vm);

	return ucv_boolean_new(true);
}

static uc_value_t *
uc_luci_translate(uc_vm_t *vm, size_t nargs) {
	uc_value_t *key = uc_fn_arg(0);
	uc_value_t *ctx = uc_fn_arg(1);
	int trlen;
	char *tr;

	if (ucv_type(key) != UC_STRING)
		return NULL;

	if (ctx && ucv_type(ctx) != UC_STRING)
		return NULL;

	if (lmo_translate_ctxt(ucv_string_get(key), ucv_string_length(key),
	                       ucv_string_get(ctx), ucv_string_length(ctx),
	                       &tr, &trlen) != 0)
		return NULL;

	return ucv_string_new_length(tr, (size_t)trlen);
}

static uc_value_t *
uc_luci_ntranslate(uc_vm_t *vm, size_t nargs) {
	uc_value_t *cnt  = uc_fn_arg(0);
	uc_value_t *skey = uc_fn_arg(1);
	uc_value_t *pkey = uc_fn_arg(2);
	uc_value_t *ctx  = uc_fn_arg(3);
	int trlen;
	char *tr;

	if (ucv_type(skey) != UC_STRING || ucv_type(pkey) != UC_STRING)
		return NULL;

	if (ctx && ucv_type(ctx) != UC_STRING)
		return NULL;

	if (lmo_translate_plural_ctxt(ucv_int64_get(cnt),
	                              ucv_string_get(skey), ucv_string_length(skey),
	                              ucv_string_get(pkey), ucv_string_length(pkey),
	                              ucv_string_get(ctx), ucv_string_length(ctx),
	                              &tr, &trlen) != 0)
		return NULL;

	return ucv_string_new_length(tr, (size_t)trlen);
}

static uc_value_t *
uc_luci_hash(uc_vm_t *vm, size_t nargs) {
	uc_value_t *key = uc_fn_arg(0);
	uc_value_t *init = uc_fn_arg(1);

	if (ucv_type(key) != UC_STRING)
		return NULL;

	if (init && ucv_type(init) != UC_INTEGER)
		return NULL;

	return ucv_uint64_new(sfh_hash(ucv_string_get(key), ucv_string_length(key),
	                               init ? ucv_uint64_get(init) : ucv_string_length(key)));
}


/* user functions */

static uc_value_t *
uc_luci_getspnam(uc_vm_t *vm, size_t nargs) {
	uc_value_t *name = uc_fn_arg(0), *rv;
	struct spwd *s;

	if (ucv_type(name) != UC_STRING)
		return NULL;

	s = getspnam(ucv_string_get(name));

	if (!s)
		return NULL;

	rv = ucv_object_new(vm);

	ucv_object_add(rv, "namp", ucv_string_new(s->sp_namp));
	ucv_object_add(rv, "pwdp", ucv_string_new(s->sp_pwdp));
	ucv_object_add(rv, "lstchg", ucv_int64_new(s->sp_lstchg));
	ucv_object_add(rv, "min", ucv_int64_new(s->sp_min));
	ucv_object_add(rv, "max", ucv_int64_new(s->sp_max));
	ucv_object_add(rv, "warn", ucv_int64_new(s->sp_warn));
	ucv_object_add(rv, "inact", ucv_int64_new(s->sp_inact));
	ucv_object_add(rv, "expire", ucv_int64_new(s->sp_expire));

	return rv;
}

static uc_value_t *
uc_luci_getpwnam(uc_vm_t *vm, size_t nargs) {
	uc_value_t *name = uc_fn_arg(0), *rv;
	struct passwd *p;

	if (ucv_type(name) != UC_STRING)
		return NULL;

	p = getpwnam(ucv_string_get(name));

	if (!p)
		return NULL;

	rv = ucv_object_new(vm);

	ucv_object_add(rv, "name", ucv_string_new(p->pw_name));
	ucv_object_add(rv, "passwd", ucv_string_new(p->pw_passwd));
	ucv_object_add(rv, "uid", ucv_int64_new(p->pw_uid));
	ucv_object_add(rv, "gid", ucv_int64_new(p->pw_gid));
	ucv_object_add(rv, "gecos", ucv_string_new(p->pw_gecos));
	ucv_object_add(rv, "dir", ucv_string_new(p->pw_dir));
	ucv_object_add(rv, "shell", ucv_string_new(p->pw_shell));

	return rv;
}

static uc_value_t *
uc_luci_crypt(uc_vm_t *vm, size_t nargs) {
	uc_value_t *phrase = uc_fn_arg(0);
	uc_value_t *setting = uc_fn_arg(1);
	char *hash;

	if (ucv_type(phrase) != UC_STRING || ucv_type(setting) != UC_STRING)
		return NULL;

	errno = 0;
	hash = crypt(ucv_string_get(phrase), ucv_string_get(setting));

	if (hash == NULL || errno != 0)
		return NULL;

	return ucv_string_new(hash);
}

static uc_value_t *
uc_luci_getuid(uc_vm_t *vm, size_t nargs) {
	return ucv_int64_new(getuid());
}

static uc_value_t *
uc_luci_getgid(uc_vm_t *vm, size_t nargs) {
	return ucv_int64_new(getgid());
}

static uc_value_t *
uc_luci_setuid(uc_vm_t *vm, size_t nargs) {
	uc_value_t *uid = uc_fn_arg(0);

	if (ucv_type(uid) != UC_INTEGER)
		return NULL;

	return ucv_boolean_new(setuid(ucv_int64_get(uid)) == 0);
}

static uc_value_t *
uc_luci_setgid(uc_vm_t *vm, size_t nargs) {
	uc_value_t *gid = uc_fn_arg(0);

	if (ucv_type(gid) != UC_INTEGER)
		return NULL;

	return ucv_boolean_new(setgid(ucv_int64_get(gid)) == 0);
}


/* misc functions */

static uc_value_t *
uc_luci_kill(uc_vm_t *vm, size_t nargs) {
	uc_value_t *pid = uc_fn_arg(0);
	uc_value_t *sig = uc_fn_arg(1);

	if (ucv_type(pid) != UC_INTEGER || ucv_type(sig) != UC_INTEGER)
		return NULL;

	return ucv_boolean_new(kill(ucv_int64_get(pid), ucv_int64_get(sig)) == 0);
}

static uc_value_t *
uc_luci_uname(uc_vm_t *vm, size_t nargs) {
	struct utsname u;
	uc_value_t *rv;

	if (uname(&u) == -1)
		return NULL;

	rv = ucv_object_new(vm);

	ucv_object_add(rv, "sysname", ucv_string_new(u.sysname));
	ucv_object_add(rv, "nodename", ucv_string_new(u.nodename));
	ucv_object_add(rv, "release", ucv_string_new(u.release));
	ucv_object_add(rv, "version", ucv_string_new(u.version));
	ucv_object_add(rv, "machine", ucv_string_new(u.machine));

	return rv;
}

static uc_value_t *
uc_luci_sysinfo(uc_vm_t *vm, size_t nargs) {
	uc_value_t *rv, *loads;
	struct sysinfo i;

	if (sysinfo(&i) == -1)
		return NULL;

	rv = ucv_object_new(vm);
	loads = ucv_array_new_length(vm, 3);

	ucv_array_push(loads, ucv_uint64_new(i.loads[0]));
	ucv_array_push(loads, ucv_uint64_new(i.loads[1]));
	ucv_array_push(loads, ucv_uint64_new(i.loads[2]));

	ucv_object_add(rv, "uptime", ucv_int64_new(i.uptime));
	ucv_object_add(rv, "loads", loads);
	ucv_object_add(rv, "totalram", ucv_uint64_new(i.totalram));
	ucv_object_add(rv, "freeram", ucv_uint64_new(i.freeram));
	ucv_object_add(rv, "sharedram", ucv_uint64_new(i.sharedram));
	ucv_object_add(rv, "bufferram", ucv_uint64_new(i.bufferram));
	ucv_object_add(rv, "totalswap", ucv_uint64_new(i.totalswap));
	ucv_object_add(rv, "freeswap", ucv_uint64_new(i.freeswap));
	ucv_object_add(rv, "procs", ucv_uint64_new(i.procs));
	ucv_object_add(rv, "totalhigh", ucv_uint64_new(i.totalhigh));
	ucv_object_add(rv, "freehigh", ucv_uint64_new(i.freehigh));
	ucv_object_add(rv, "mem_unit", ucv_uint64_new(i.mem_unit));

	return rv;
}

static uc_value_t *
uc_luci_statvfs(uc_vm_t *vm, size_t nargs) {
	uc_value_t *path = uc_fn_arg(0), *rv;
	struct statvfs s;

	if (ucv_type(path) != UC_STRING)
		return NULL;

	if (statvfs(ucv_string_get(path), &s) == -1)
		return NULL;

	rv = ucv_object_new(vm);

	ucv_object_add(rv, "bsize", ucv_uint64_new(s.f_bsize));
	ucv_object_add(rv, "frsize", ucv_uint64_new(s.f_frsize));

	ucv_object_add(rv, "blocks", ucv_uint64_new(s.f_blocks));
	ucv_object_add(rv, "bfree", ucv_uint64_new(s.f_bfree));
	ucv_object_add(rv, "bavail", ucv_uint64_new(s.f_bavail));

	ucv_object_add(rv, "files", ucv_uint64_new(s.f_files));
	ucv_object_add(rv, "ffree", ucv_uint64_new(s.f_ffree));
	ucv_object_add(rv, "favail", ucv_uint64_new(s.f_favail));

	ucv_object_add(rv, "fsid", ucv_uint64_new(s.f_fsid));
	ucv_object_add(rv, "flag", ucv_uint64_new(s.f_flag));
	ucv_object_add(rv, "namemax", ucv_uint64_new(s.f_namemax));

	return rv;
}



struct link_mode_def {
    int bit;
    int speed;
    const char *duplex;
};

// Based on standard ETHTOOL_LINK_MODE_*_BIT
static const struct link_mode_def link_modes[] = {
    {0, 10, "half"}, {1, 10, "full"}, {2, 100, "half"}, {3, 100, "full"}, {4, 1000, "half"}, {5, 1000, "full"},
    {12, 10000, "full"}, {15, 2500, "full"}, {17, 1000, "full"}, {18, 10000, "full"}, {19, 10000, "full"},
    {21, 20000, "full"}, {22, 20000, "full"}, {23, 40000, "full"}, {24, 40000, "full"}, {25, 40000, "full"},
    {26, 40000, "full"}, {27, 56000, "full"}, {28, 56000, "full"}, {29, 56000, "full"}, {30, 56000, "full"},
    {31, 25000, "full"}, {32, 25000, "full"}, {33, 25000, "full"}, {34, 50000, "full"}, {35, 50000, "full"},
    {36, 100000, "full"}, {37, 100000, "full"}, {38, 100000, "full"}, {39, 100000, "full"}, {40, 50000, "full"},
    {41, 1000, "full"}, {42, 10000, "full"}, {43, 10000, "full"}, {44, 10000, "full"}, {45, 10000, "full"},
    {46, 10000, "full"}, {47, 2500, "full"}, {48, 5000, "full"}, {52, 50000, "full"}, {53, 50000, "full"},
    {54, 50000, "full"}, {55, 50000, "full"}, {56, 50000, "full"}, {57, 100000, "full"}, {58, 100000, "full"},
    {59, 100000, "full"}, {60, 100000, "full"}, {61, 100000, "full"}, {62, 200000, "full"}, {63, 200000, "full"},
    {64, 200000, "full"}, {65, 200000, "full"}, {66, 200000, "full"}, {67, 100, "full"}, {68, 1000, "full"},
    {69, 400000, "full"}, {70, 400000, "full"}, {71, 400000, "full"}, {72, 400000, "full"}, {73, 400000, "full"},
    {75, 100000, "full"}, {76, 100000, "full"}, {77, 100000, "full"}, {78, 100000, "full"}, {79, 100000, "full"},
    {80, 200000, "full"}, {81, 200000, "full"}, {82, 200000, "full"}, {83, 200000, "full"}, {84, 200000, "full"},
    {85, 400000, "full"}, {86, 400000, "full"}, {87, 400000, "full"}, {88, 400000, "full"}, {89, 400000, "full"},
    {90, 100, "half"}, {91, 100, "full"}, {92, 10, "full"}, {93, 800000, "full"}, {94, 800000, "full"},
    {95, 800000, "full"}, {96, 800000, "full"}, {97, 800000, "full"}, {98, 800000, "full"}, {99, 10, "full"},
    {100, 10, "half"}, {101, 10, "half"}, {102, 10, "full"}, {103, 200000, "full"}, {104, 200000, "full"},
    {105, 200000, "full"}, {106, 200000, "full"}, {107, 200000, "full"}, {108, 200000, "full"}, {109, 400000, "full"},
    {110, 400000, "full"}, {111, 400000, "full"}, {112, 400000, "full"}, {113, 400000, "full"}, {114, 400000, "full"},
    {115, 800000, "full"}, {116, 800000, "full"}, {117, 800000, "full"}, {118, 800000, "full"}, {119, 800000, "full"},
    {120, 800000, "full"}, {121, 1600000, "full"}, {122, 1600000, "full"}, {123, 1600000, "full"}, {124, 1600000, "full"}
};
#define NUM_LINK_MODES (sizeof(link_modes) / sizeof(link_modes[0]))

static void
add_mode(uc_vm_t *vm, uc_value_t *arr, int speed, const char *duplex) {
    uc_value_t *obj = ucv_object_new(vm);
    ucv_object_add(obj, "speed", ucv_int64_new(speed));
    ucv_object_add(obj, "duplex", ucv_string_new(duplex));
    ucv_array_push(arr, obj);
}

static uc_value_t *
uc_luci_ethtool(uc_vm_t *vm, size_t nargs) {
    uc_value_t *ifname = uc_fn_arg(0);
    uc_value_t *arr = ucv_array_new(vm);

    if (ucv_type(ifname) != UC_STRING)
        return arr;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return arr;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ucv_string_get(ifname), IFNAMSIZ - 1);

    struct {
        struct ethtool_link_settings req;
        __u32 link_mode_data[3 * 127];
    } ecmd;

    memset(&ecmd, 0, sizeof(ecmd));
    ecmd.req.cmd = ETHTOOL_GLINKSETTINGS;
    ifr.ifr_data = (caddr_t)&ecmd;

    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0) {
        struct ethtool_cmd ecmd_old;
        memset(&ecmd_old, 0, sizeof(ecmd_old));
        ecmd_old.cmd = ETHTOOL_GSET;
        ifr.ifr_data = (caddr_t)&ecmd_old;

        if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
            for (size_t i = 0; i < NUM_LINK_MODES; i++) {
                if (link_modes[i].bit < 32 && (ecmd_old.supported & (1U << link_modes[i].bit))) {
                    add_mode(vm, arr, link_modes[i].speed, link_modes[i].duplex);
                }
            }
        }
    } else {
        if (ecmd.req.link_mode_masks_nwords < 0) {
            ecmd.req.link_mode_masks_nwords = -ecmd.req.link_mode_masks_nwords;
            if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
                for (size_t i = 0; i < NUM_LINK_MODES; i++) {
                    int bit = link_modes[i].bit;
                    int word_index = bit / 32;
                    int bit_index = bit % 32;
                    if (word_index < ecmd.req.link_mode_masks_nwords &&
                        (ecmd.link_mode_data[word_index] & (1U << bit_index))) {
                        add_mode(vm, arr, link_modes[i].speed, link_modes[i].duplex);
                    }
                }
            }
        }
    }

    close(fd);
    return arr;
}


static const uc_function_list_t luci_fns[] = {
	{ "load_catalog",		uc_luci_load_catalog },
	{ "close_catalog",		uc_luci_close_catalog },
	{ "change_catalog",		uc_luci_change_catalog },
	{ "get_translations",	uc_luci_get_translations },
	{ "translate",			uc_luci_translate },
	{ "ntranslate",			uc_luci_ntranslate },
	{ "hash",				uc_luci_hash },

	{ "getspnam",			uc_luci_getspnam },
	{ "getpwnam",			uc_luci_getpwnam },
	{ "crypt",				uc_luci_crypt },
	{ "getuid",				uc_luci_getuid },
	{ "setuid",				uc_luci_setuid },
	{ "getgid",				uc_luci_getgid },
	{ "setgid",				uc_luci_setgid },

	{ "kill",				uc_luci_kill },
	{ "uname",				uc_luci_uname },
	{ "sysinfo",			uc_luci_sysinfo },
	{ "statvfs",			uc_luci_statvfs },
	{ "ethtool",			uc_luci_ethtool },
};


void uc_module_init(uc_vm_t *vm, uc_value_t *scope)
{
	uc_function_list_register(scope, luci_fns);
}
