import os


def mkpath(name, mode=0o777, verbose=1, dry_run=0):
	os.makedirs(name, mode, exist_ok=True)
