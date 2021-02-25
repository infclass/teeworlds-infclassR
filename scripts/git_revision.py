import errno
import subprocess
try:
	from subprocess import DEVNULL
except ImportError:
	import os
	DEVNULL = open(os.devnull, 'wb')
try:
	git_hash = subprocess.check_output(["git", "describe", "--tags", "--abbrev=12", "--always", "HEAD"], stderr=DEVNULL).decode().strip()
	definition = '"{}"'.format(git_hash)
except FileNotFoundError as e:
	if e.errno != errno.ENOENT:
		raise
	definition = "0"
except subprocess.CalledProcessError:
	definition = "0"
print("const char *GIT_SHORTREV_HASH = {};".format(definition))
