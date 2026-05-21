#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <iconv.h>
#include <algorithm>
#include <memory>
#include <linux/hdreg.h>

// Convert UTF-8 to GBK using iconv for high-fidelity terminal compatibility
std::string Utf8ToGbk(const std::string& utf8_str) {
    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == (iconv_t)-1) {
        return utf8_str;
    }
    
    size_t in_bytes = utf8_str.size();
    size_t out_bytes = in_bytes * 2 + 1;
    std::vector<char> out_buf(out_bytes, 0);
    
    char* in_ptr = const_cast<char*>(utf8_str.data());
    char* out_ptr = out_buf.data();
    
    size_t res = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
    iconv_close(cd);
    
    if (res == (size_t)-1) {
        return utf8_str;
    }
    
    return std::string(out_buf.data(), out_buf.size() - out_bytes);
}

// Print to stdout with GBK conversion
void PrintGbk(const std::string& utf8_str, bool newline = true) {
    std::cout << Utf8ToGbk(utf8_str);
    if (newline) {
        std::cout << std::endl;
    }
}

// Get timestamp in HH:MM:SS:Usec format (microseconds not zero-padded, mimicking %ld)
std::string GetTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s:%ld", time_buf, (long)tv.tv_usec);
    return buf;
}

// Log message with timestamp
void Log(const std::string& utf8_str) {
    PrintGbk(GetTimestamp() + " - " + utf8_str);
}

// Execute external command and return stdout
std::string ExecCommand(const std::string& cmd, int& exit_code) {
    std::string result;
    char buffer[256];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = -1;
    }
    return result;
}

struct InterfaceInfo {
    std::string ip;
    std::string mac;
};

// Get MAC address for a given interface name (lowercase, no colons)
std::string GetMacAddress(const std::string& if_name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "";
    
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, if_name.c_str(), IFNAMSIZ - 1);
    
    std::string mac_str = "";
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        mac_str = buf;
    }
    close(fd);
    return mac_str;
}

// Get non-loopback IPv4 interfaces
std::vector<InterfaceInfo> GetInterfaces() {
    std::vector<InterfaceInfo> list;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return list;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            std::string name = ifa->ifa_name;
            if (name == "lo" || (ifa->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            
            char ip_buf[INET_ADDRSTRLEN];
            struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            inet_ntop(AF_INET, &(ipv4->sin_addr), ip_buf, INET_ADDRSTRLEN);
            
            InterfaceInfo info;
            info.ip = ip_buf;
            info.mac = GetMacAddress(name);
            list.push_back(info);
        }
    }
    freeifaddrs(ifaddr);
    return list;
}

// Get device name (hostname)
std::string GetDeviceName() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
    return "";
}

// Get OS version (truncated to the second dot)
std::string GetOSVersion() {
    struct utsname uts;
    if (uname(&uts) == -1) {
        return "";
    }
    std::string release = uts.release;
    size_t first_dot = release.find('.');
    if (first_dot == std::string::npos) {
        return release;
    }
    size_t second_dot = release.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return release;
    }
    return release.substr(0, second_dot + 1);
}

int main() {
    // 1. Check super-user privileges and print warnings to stderr
    if (getuid() != 0) {
        std::cerr << "WARNING: you should run this program as super-user." << std::endl;
        std::cerr << "WARNING: output may be incomplete or inaccurate, you should run this program as super-user." << std::endl;
    }
    
    // 2. Start execution
    PrintGbk("datacollect for linux version 1.2.5");
    Log("开始获取终端各种信息");
    
    // Terminal Type
    Log("开始获取终端类型");
    int term_type = 2; // Hardcoded 2 for Linux
    Log("获取终端类型完成");
    
    // System Time
    Log("开始获取系统时间");
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    char date_buf[64];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    std::string collect_time = date_buf;
    Log("获取系统时间完成");
    
    // IP and MAC Addresses
    Log("开始获取IP和MAC信息");
    auto interfaces = GetInterfaces();
    std::string ip1 = "";
    std::string ip2 = "";
    std::string mac1 = "";
    std::string mac2 = "";
    if (interfaces.size() >= 1) {
        ip1 = interfaces[0].ip;
        mac1 = interfaces[0].mac;
    }
    if (interfaces.size() >= 2) {
        ip2 = interfaces[1].ip;
        mac2 = interfaces[1].mac;
    }
    Log("获取IP和MAC信息完成");
    
    // Device Name and OS Version
    Log("开始获取设备名和系统版本");
    std::string hostname = GetDeviceName();
    std::string os_version = GetOSVersion();
    Log("获取设备名和系统版本完成");
    
    // Disk Serial
    Log("开始获取硬盘序列号");
    std::string disk_serial = "";
    std::vector<std::string> open_errors;
    
    int fd = open("/dev/hda", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        open_errors.push_back("IDE Disk打开失败，fd：" + std::to_string(fd) + "，errno：" + std::to_string(errno) + "，errmsg：" + strerror(errno));
        fd = open("/dev/sda", O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            open_errors.push_back("SCSI device打开失败，fd：" + std::to_string(fd) + "，errno：" + std::to_string(errno) + "，errmsg：" + strerror(errno));
        }
    }
    
    if (fd < 0) {
        // Both opened failed, print open failure logs
        for (const auto& err : open_errors) {
            Log(err);
        }
    } else {
        // One opened successfully, try ioctl
        struct hd_driveid id;
        int ret = ioctl(fd, HDIO_GET_IDENTITY, &id);
        if (ret < 0) {
            Log("ioctl调用失败，ret：" + std::to_string(ret) + "，errno：" + std::to_string(errno) + "，errmsg：" + strerror(errno));
        } else {
            char serial_buf[40];
            std::memset(serial_buf, 0, sizeof(serial_buf));
            std::memcpy(serial_buf, id.serial_no, sizeof(id.serial_no));
            disk_serial = serial_buf;
            // Trim leading/trailing spaces
            disk_serial.erase(0, disk_serial.find_first_not_of(" \t\r\n"));
            disk_serial.erase(disk_serial.find_last_not_of(" \t\r\n") + 1);
        }
        close(fd);
    }
    
    // If we couldn't get it via ioctl (or opens failed), fall back to lshw
    if (disk_serial.empty()) {
        int disk_exit = 0;
        std::string disk_cmd = "lshw -class disk|grep serial";
        std::string disk_output = ExecCommand(disk_cmd, disk_exit);
        if (disk_exit != 0) {
            Log("调用失败：Errno：" + std::to_string(disk_exit) + "，未匹配到内容或权限不足等一般性错误: " + disk_cmd);
            Log("尝试lshw获取硬盘序列号失败");
        } else {
            size_t pos = disk_output.find("serial:");
            if (pos != std::string::npos) {
                disk_serial = disk_output.substr(pos + 7);
                disk_serial.erase(0, disk_serial.find_first_not_of(" \t\r\n"));
                disk_serial.erase(disk_serial.find_last_not_of(" \t\r\n") + 1);
            }
            if (disk_serial.empty()) {
                Log("尝试lshw获取硬盘序列号失败");
            }
        }
    }
    Log("获取硬盘序列号完成");
    
    // CPU Serial
    Log("开始获取CPU序列号");
    std::string cpu_cmd = "dmidecode -t 4 | grep ID";
    int cpu_exit = 0;
    std::string cpu_output = ExecCommand(cpu_cmd, cpu_exit);
    std::string cpu_serial = "";
    if (cpu_exit != 0) {
        Log("调用失败：Errno：" + std::to_string(cpu_exit) + "，未匹配到内容或权限不足等一般性错误: " + cpu_cmd);
    } else {
        size_t pos = cpu_output.find("ID:");
        if (pos != std::string::npos) {
            // Find the end of that first line containing "ID:"
            size_t end_line = cpu_output.find('\n', pos);
            std::string first_id_line = cpu_output.substr(pos + 3, (end_line == std::string::npos) ? std::string::npos : (end_line - (pos + 3)));
            
            // Clean spaces and all non-alphanumeric characters
            std::string clean_id = "";
            for (char c : first_id_line) {
                if (std::isalnum(c)) {
                    clean_id += c;
                }
            }
            cpu_serial = clean_id;
            // Convert to uppercase to match the original GBK program output perfectly
            std::transform(cpu_serial.begin(), cpu_serial.end(), cpu_serial.begin(), ::toupper);
        }
    }
    Log("获取CPU序列号完成");
    
    // BIOS Serial
    Log("开始获取BIOS序列号");
    std::string bios_cmd = "dmidecode -t 1 | grep \"Serial Number\"";
    int bios_exit = 0;
    std::string bios_output = ExecCommand(bios_cmd, bios_exit);
    std::string bios_serial = "";
    if (bios_exit != 0) {
        Log("调用失败：Errno：" + std::to_string(bios_exit) + "，未匹配到内容或权限不足等一般性错误: " + bios_cmd);
    } else {
        size_t pos = bios_output.find("Serial Number:");
        if (pos != std::string::npos) {
            bios_serial = bios_output.substr(pos + 14);
            bios_serial.erase(0, bios_serial.find_first_not_of(" \t\r\n"));
            bios_serial.erase(bios_serial.find_last_not_of(" \t\r\n") + 1);
        }
    }
    Log("获取BIOS序列号完成");
    
    // 3. Print Results Summary
    PrintGbk("");
    PrintGbk("终端类型: " + std::to_string(term_type));
    PrintGbk("信息采集时间: " + collect_time);
    PrintGbk("私网IP1: " + ip1);
    PrintGbk("私网IP2: " + ip2);
    PrintGbk("网卡MAC地址1: " + mac1);
    PrintGbk("网卡MAC地址2: " + mac2);
    PrintGbk("设备名: " + hostname);
    PrintGbk("操作系统版本: " + os_version);
    PrintGbk("硬盘序列号: " + disk_serial);
    PrintGbk("CPU序列号: " + cpu_serial);
    PrintGbk("BIOS序列号: " + bios_serial);
    PrintGbk("");
    
    // CollectData block
    std::string collect_data = "CollectData = [" +
                               std::to_string(term_type) + "@" +
                               collect_time + "@" +
                               ip1 + "@" +
                               ip2 + "@" +
                               mac1 + "@" +
                               mac2 + "@" +
                               hostname + "@" +
                               os_version + "@" +
                               disk_serial + "@" +
                               cpu_serial + "@" +
                               bios_serial + "]";
    PrintGbk(collect_data);
    
    return 0;
}
