#!/bin/sh

#.. |el_cleanup| replace:: :ref:`el_cleanup(3) <manuals/base/el_cleanup.3:el_cleanup>`
cd $(dirname "$0")
truncate -s0 ref-list.in

for r in $(grep -Eho '\|[a-zA-Z_]+\|' *.in ../rb_overview.7.rst | sort | uniq | tr -d '|'); do
	path=$(find ../manuals -name "$r.3.rst" | cut -f2- -d/)
	path="${path%.*}"
	if [ "$path" ]; then
		echo ".. |$r| replace:: :ref:\`$r(3) <$path:$r>\`" >> ref-list.in
	else
		echo ".. |$r| replace:: :ref:\`$r <$r>\`" >> ref-list.in
	fi
done
