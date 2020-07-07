import makedfs
import os
import sys

def infhex(i):
    return hex(i)[2:].upper()

# TODO: Could do proper parsing using argparse
ssd_filename = sys.argv[1]
vdfs_base = sys.argv[2]

disk = makedfs.Disk()
disk.open(open(ssd_filename, 'rb'))

if not os.path.isdir(vdfs_base):
    os.mkdir(vdfs_base)

ssd_files = disk.catalogue().read()[1]
for ssd_file in ssd_files:
    dirname = ssd_file.name[0]
    leafname = ssd_file.name[2:]
    if dirname == '$':
        vdfs_dir = vdfs_base
    else:
        vdfs_dir = os.path.join(vdfs_base, dirname)
        if not os.path.isdir(vdfs_dir):
            os.mkdir(vdfs_dir)
    vdfs_name = os.path.join(vdfs_dir, leafname)
    # TODO: Should have command line option to say whether overwriting is OK or not
    # TODO: It's possible an SSD could contain e.g. "C.FOO" and "$.C" and we'd try to
    # create "C" as both a directory and a file in that case; need to handle that
    # better.
    with open(vdfs_name, 'wb') as f:
        f.write(ssd_file.data)
    with open(vdfs_name + '.inf', 'wb') as f:
        f.write(ssd_file.name[2:] + ' ' + infhex(ssd_file.load_address) + ' ' + infhex(ssd_file.execution_address))
