command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro
  install_libdir=/usr/lib
  sudo apt-get update
  sudo apt-get install -y --force-yes \
                 git wget build-essential \
                 libssl-dev libmariadbclient-dev php5 perl \
                 coreutils realpath libjansson-dev openjdk-7-jdk
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
                 libssl-dev libmariadbclient-dev php5 perl \
                 coreutils realpath libjansson-devel openjdk-7-jdk
  else
  # YUM!
    sudo yum clean all
    sudo yum update -y
    sudo yum install -y --nogpgcheck git wget gcc gcc-c++ 
                 libssl-dev libmariadbclient-devel php5 perl \
                 coreutils realpath libjansson-dev openjdk-7-jdk
  fi
fi

pip install JayDeBeApi
