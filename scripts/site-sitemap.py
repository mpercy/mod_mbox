#!/usr/local/bin/python

import os
from os.path import join as pjoin
import sys
import subprocess

def get_output(cmd):
    s = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out = s.communicate()[0]
    s.wait()
    return out.strip()

# you could use os.path.walk to calculate this... or you could use du(1).
def duhack(path):
    cmd = ['du', '-k', path]
    out = get_output(cmd).split()
    return int(out[0]) * 1024

BASEPATH=sys.argv[1]
ROOT="/x1/mail-archives/mod_mbox"
HOSTNAME="http://mail-archives.apache.org/mod_mbox/"
PARITION_SIZE=100 * 1024 * 1024
tlps={}
for files in os.listdir(ROOT):
    path = files
    tlp = path[0:path.find('-')]
    list = path[path.find('-')+1:]
    # print "%s - %s %s" % (tlp, list, path)
    if not os.access("%s/%s/listinfo.db" % (ROOT, path), os.F_OK):
        continue
    if tlp == "www":
       tlp = "asf"
    if not tlps.has_key(tlp):
        tlps[tlp] = {}
    tlps[tlp][list] = [path, duhack(pjoin(ROOT, path))]

keys = tlps.keys()
keys.sort()

count = 0
fcount = 0
def write_sitemap_header(fp):
    fp.write("""<?xml version="1.0" encoding="UTF-8"?>\n<sitemapindex xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n""")
def write_sitemap_footer(fp):
    fp.write("</sitemapindex>\n")

fp = open(BASEPATH % (fcount), 'w')

write_sitemap_header(fp)

for tlp in keys:
    klist = tlps[tlp].keys()
    klist.sort()
    for list in klist:
        name = tlps[tlp][list][0]
        size = tlps[tlp][list][1]
        if size < PARITION_SIZE:
            count += 1
            fp.write("<sitemap><loc>%s%s/?format=sitemap</loc></sitemap>\n" % (HOSTNAME, name))
        else:
            part = (size / PARITION_SIZE) + 1
            for i in range(0, part):
                count += 1
                fp.write("<sitemap><loc>%s%s/?format=sitemap&amp;pmax=%d&amp;part=%d</loc></sitemap>\n" % (HOSTNAME, name, part, i))
        if count > 500:
            write_sitemap_footer(fp)
            fp.close()
            count = 0
            fcount += 1
            fp = open(BASEPATH  % (fcount), 'w')
            write_sitemap_header(fp)

write_sitemap_footer(fp)


