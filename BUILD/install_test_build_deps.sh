
rp=`realpath $0`
export src_dir=`dirname $rp`

command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro
  install_libdir=/usr/lib
  source /etc/os-release
  echo "deb http://mirror.netinch.com/pub/mariadb/repo/10.3/ubuntu/ ${UBUNTU_CODENAME} main" > mariadb.list
  sudo cp mariadb.list /etc/apt/sources.list.d/
  sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 0xF1656F24C74CD1D8
  sudo apt-get update
  sudo apt-get install -y --force-yes \
                 git wget build-essential \
                 libssl-dev libmariadbclient-dev php perl \
                 coreutils libjansson-dev openjdk-8-jdk \
                 mariadb-test python python-pip cmake libpam0g-dev
else
  ## RPM-based distro
  install_libdir=/usr/lib64
  command -v yum

  if [ $? != 0 ]
  then
    # We need zypper here
    sudo zypper -n refresh
    sudo zypper -n update
    sudo zypper -n install gcc gcc-c++ \
                 libgcrypt-devel libopenssl-devel mariadb-devel mariadb-test \
                 php perl coreutils libjansson-devel openjdk-8-jdk python python-pip \
                 cmake pam-devel
  else
  # YUM!
    cat >mariadb.repo <<EOL
[mariadb]
name = MariaDB
baseurl = http://yum.mariadb.org/10.3/centos/$releasever/$basearch/
gpgkey=https://yum.mariadb.org/RPM-GPG-KEY-MariaDB
gpgcheck=0
EOL
    sudo cp mariadb.repo /etc/yum.repos.d/
    sudo yum clean all
    sudo yum update -y
    sudo yum install -y --nogpgcheck git wget gcc gcc-c++ 
                 libgcrypt-devel libopenssl-devel mariadb-devel mariadb-test \
                 php perl coreutils libjansson-devel openjdk-8-jdk python python-pip \
                 cmake pam-devel
  fi
fi

${src_dir}/install_cmake.sh
pip install JayDeBeApi
