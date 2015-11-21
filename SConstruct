#!/usr/bin/env scons
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import subprocess
from os.path import join as pjoin

EnsureSConsVersion(1, 1, 0)


opts = Variables('build.py')

opts.Add(PathVariable('APXS', 'Path apxs','/usr/local/bin/apxs'))
opts.Add('DBM', 'Choose dbm implementation (default, sdbm, db, ...)', 'default')

env = Environment(options=opts, CPPDEFINES={'-DDBM_TYPE' : '${DBM}'})

def get_output(cmd):
    s = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out = s.communicate()[0]
    s.wait()
    return out.strip()
    
def apxs_query(path, key):
    cmd = [path, "-q", key]
    return get_output(cmd)

apr_config = apxs_query(env["APXS"], 'APR_CONFIG')
apu_config = apxs_query(env["APXS"], 'APU_CONFIG')

env.Replace(CC = apxs_query(env["APXS"], 'CC'))
env.Replace(CPP = apxs_query(env["APXS"], 'CPP'))

env.ParseConfig(apr_config + ' --cflags --cppflags --includes')
env.ParseConfig(apu_config + ' --includes')

# TODO: Move to httpd-config when it comes out !
env.ParseConfig(env['APXS'] + ' -q EXTRA_CFLAGS')
env.ParseConfig(env['APXS'] + ' -q EXTRA_CPPFLAGS')

env.AppendUnique(CPPPATH = [apxs_query(env['APXS'], 'exp_includedir')])
if env['PLATFORM'] == 'darwin':
    env.AppendUnique(LINKFLAGS = ['-undefined', 'dynamic_lookup'])

libsources = [pjoin('module-2.0', x) for x in Split("""
    mbox_cache.c
    mbox_parse.c
    mbox_sort.c
    mbox_thread.c
    mbox_externals.c
""")]

lib = env.StaticLibrary(target = "libmbox", source = [ libsources])

modsources = [pjoin('module-2.0', x) for x in Split("""
    mod_mbox.c
    mod_mbox_file.c
    mod_mbox_out.c
    mod_mbox_index.c
    mod_mbox_cte.c
    mod_mbox_mime.c
    mod_mbox_sitemap.c
""")]

module = env.LoadableModule(target = "mod_mbox.so", source = [modsources, libsources], SHLIBPREFIX='')

lenv = env.Clone()

# This is a hack to set the RPATH on some operating systems... make me more
# portable later....
p1 = get_output([apr_config, '--bindir'])
p1 = pjoin(p1[:p1.rfind('/')], 'lib')
p2 = get_output([apu_config, '--bindir'])
p2 = pjoin(p2[:p2.rfind('/')], 'lib')
lenv.AppendUnique(RPATH = [p1, p2])

lenv.ParseConfig(apr_config + ' --link-ld')
lenv.ParseConfig(apu_config + ' --link-ld')
util = lenv.Program(target = 'mod-mbox-util', source = ['module-2.0/mod-mbox-util.c', lib])

mod_path = apxs_query(env["APXS"], 'exp_libexecdir')
bin_path = apxs_query(env["APXS"], 'exp_bindir')
imod = env.Install(mod_path, source = [module])
bmod = env.Install(bin_path, source = [util])
env.Alias('install', [imod, bmod])

targets = [module, util]

env.Default(targets)
