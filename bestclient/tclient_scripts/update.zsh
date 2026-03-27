#!/usr/bin/env zsh

set -e -x
setopt extended_glob

function mcp {
  cp "$1" "$2.$$.tmp" && mv "$2.$$.tmp" "$2"
}

OLD_VERSION=$1
VERSION=$2

renice -n 19 -p $$
ionice -c 3 -p $$

# Set directories
SCRIPTS_DIR="/var/www/BestClient.app/scripts"
UPDATES_DIR="/var/www/BestClient.app/updates"

cd "$SCRIPTS_DIR"

# Download from github
wget -O BestClient-${OLD_VERSION}-win64.zip "https://github.com/sjrc6/TaterClient-ddnet/releases/download/V${OLD_VERSION}/BestClient-windows.zip"
wget -O BestClient-${OLD_VERSION}-linux_x86_64.tar.xz "https://github.com/sjrc6/TaterClient-ddnet/releases/download/V${OLD_VERSION}/BestClient-ubuntu.tar.xz"

wget -O BestClient-${VERSION}-win64.zip "https://github.com/sjrc6/TaterClient-ddnet/releases/download/V${VERSION}/BestClient-windows.zip"
wget -O BestClient-${VERSION}-linux_x86_64.tar.xz "https://github.com/sjrc6/TaterClient-ddnet/releases/download/V${VERSION}/BestClient-ubuntu.tar.xz"

# Unpack directories
unzip -d BestClient-${OLD_VERSION}-win64 BestClient-${OLD_VERSION}-win64.zip
unzip -d BestClient-${VERSION}-win64 BestClient-${VERSION}-win64.zip

for ver in $OLD_VERSION $VERSION; do
  WIN_DIR="BestClient-${ver}-win64"
  inner_dirs=("${(@f)$(find "$WIN_DIR" -mindepth 1 -maxdepth 1 -type d)}")
  if [ ${#inner_dirs} -eq 1 ]; then
    mv "${inner_dirs[1]}"/* "$WIN_DIR/"
    rmdir "${inner_dirs[1]}"
  fi
done

mkdir -p BestClient-${OLD_VERSION}-linux_x86_64
tar --strip-components=1 -xvf BestClient-${OLD_VERSION}-linux_x86_64.tar.xz -C BestClient-${OLD_VERSION}-linux_x86_64

mkdir -p BestClient-${VERSION}-linux_x86_64
tar --strip-components=1 -xvf BestClient-${VERSION}-linux_x86_64.tar.xz -C BestClient-${VERSION}-linux_x86_64

# fetch update.json from the served files
if [ -f "${UPDATES_DIR}/update.json" ]; then
  cp "${UPDATES_DIR}/update.json" .
else
  echo "[]" > update.json
fi

# diff versions
./diff_update.py $OLD_VERSION $VERSION

cp update.json update.json.old && mv update.json.new update.json

if [ -d "${UPDATES_DIR}/data" ]; then
  mv "${UPDATES_DIR}/data" "${UPDATES_DIR}/data.old"
fi
mv BestClient-${VERSION}-win64/data "${UPDATES_DIR}/data"
rm -rf "${UPDATES_DIR}/data.old"

mv BestClient-${VERSION}-win64/license.txt ${UPDATES_DIR}/
mv BestClient-${VERSION}-win64/storage.cfg ${UPDATES_DIR}/
mv BestClient-${VERSION}-win64/config_directory.bat ${UPDATES_DIR}/

for i in BestClient-${VERSION}-win64/*.{exe,dll}; do 
  mcp "$i" "${i:r:t}-win64.${i:e}"
  mv "${i:r:t}-win64.${i:e}" "${UPDATES_DIR}/"
done

for i in BestClient-${VERSION}-linux_x86_64/{DDNet,DDNet-Server}; do 
  mcp "$i" "${i:r:t}-linux-x86_64"
  mv "${i:r:t}-linux-x86_64" "${UPDATES_DIR}/"
done

if ls BestClient-${VERSION}-linux_x86_64/*.so 2>&1 > /dev/null; then
  for i in BestClient-${VERSION}-linux_x86_64/*.so; do 
    mcp "$i" "${i:r:t}-linux-x86_64.so"
    mv "${i:r:t}-linux-x86_64.so" "${UPDATES_DIR}/"
  done
fi

mv update.json ${UPDATES_DIR}/

rm -rf BestClient-${OLD_VERSION}-win64 BestClient-${OLD_VERSION}-linux_x86_64
rm -rf BestClient-${VERSION}-win64 BestClient-${VERSION}-linux_x86_64
rm BestClient-${OLD_VERSION}-win64.zip BestClient-${OLD_VERSION}-linux_x86_64.tar.xz
rm BestClient-${VERSION}-win64.zip BestClient-${VERSION}-linux_x86_64.tar.xz

cat <<EOF > ${UPDATES_DIR}/info.json
{
  "version": "${VERSION}"
}
EOF
