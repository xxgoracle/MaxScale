#!/bin/bash

# Install dependencies
npm i

COMMANDS=$(
for i in `node maxctrl.js --help|awk '/^$/{p=0} {if(p){print $2}}/Commands:/{p=1}'`
do
    echo "## $i"
    echo

    for j in `node maxctrl.js --help $i|awk '/^$/{p=0} {if(p){print $3}}/Commands:/{p=1}'`
    do
        echo "### $i $j"
        echo
        echo \`\`\`
        echo "`node maxctrl.js --help $i $j`"
        echo \`\`\`
        echo
    done
done
)

cat <<EOF > ../Documentation/Reference/MaxCtrl.md
# MaxCtrl

MaxCtrl is a command line administrative client for MaxScale which uses
the MaxScale REST API for communication. It is intended to be the
replacement software for the legacy MaxAdmin command line client.

By default, the MaxScale REST API listens on port 8989 on the local host. The
default credentials for the REST API are \`admin:mariadb\`. The users used by the
REST API are the same that are used by the MaxAdmin network interface. This
means that any users created for the MaxAdmin network interface should work with
the MaxScale REST API and MaxCtrl.

For more information about the MaxScale REST API, refer to the
[REST API documentation](../REST-API/API.md) and the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

[TOC]

# Commands

$COMMANDS

EOF
