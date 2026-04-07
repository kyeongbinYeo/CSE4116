#include "nvme_passthru.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <cerrno>

using namespace std;

const unsigned int PAGE_SIZE = 4096;
const unsigned int MAX_BUFLEN = 16*1024*1024;
const unsigned int NSID = 1;
const unsigned int MAX_TRANSFER_BLOCKS = 2048;

int Embedded::Proj1::Open(const std::string &dev) {
    int err;
    err = open(dev.c_str(), O_RDONLY);
    if (err < 0) return -1;
    fd_ = err;

    struct stat nvme_stat;
    err = fstat(fd_, &nvme_stat);
    if (err < 0) return -1;
    if (!S_ISCHR(nvme_stat.st_mode) && !S_ISBLK(nvme_stat.st_mode))
        return -1;

    return 0;
}

int Embedded::Proj1::nvme_passthru(uint8_t opcode, uint32_t nsid,
                                    uint64_t slba, uint16_t nlb,
                                    void *buf, uint32_t data_len)
{
    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opcode   = opcode;
    cmd.nsid     = nsid;
    cmd.addr     = (uint64_t)(uintptr_t)buf;
    cmd.data_len = data_len;
    cmd.cdw10    = slba & 0xffffffff;
    cmd.cdw11    = slba >> 32;
    cmd.cdw12    = nlb - 1;

    int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
    if (ret < 0) {
        cerr << "[ERROR] ioctl failed: " << strerror(errno) << endl;
        return -errno;
    }
    return 0;
}

int Embedded::Proj1::ImageWrite(const std::vector<uint8_t> &buf) {
    if (buf.empty()) return -EINVAL;
    if (buf.size() > MAX_BUFLEN) {
        cerr << "[ERROR] Image size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }

    size_t total_size = buf.size();
    size_t offset = 0;
    uint64_t slba = 0;

    while (offset < total_size) {
        size_t chunk = min((size_t)(MAX_TRANSFER_BLOCKS * 512), total_size - offset);
        uint16_t nlb = (uint16_t)((chunk + 511) / 512);

        void *aligned_buf;
        posix_memalign(&aligned_buf, PAGE_SIZE, nlb * 512);
        memset(aligned_buf, 0, nlb * 512);
        memcpy(aligned_buf, buf.data() + offset, chunk);

        int ret = nvme_passthru(NVME_CMD_WRITE, NSID, slba, nlb, aligned_buf, nlb * 512);
        free(aligned_buf);
        if (ret < 0) return ret;

        offset += chunk;
        slba += nlb;
    }

    return 0;
}

int Embedded::Proj1::ImageRead(std::vector<uint8_t> &buf, size_t size) {
    if (size == 0) return -EINVAL;
    if (size > MAX_BUFLEN) {
        cerr << "[ERROR] Requested read size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }

    buf.resize(size);
    size_t offset = 0;
    uint64_t slba = 0;

    while (offset < size) {
        size_t chunk = min((size_t)(MAX_TRANSFER_BLOCKS * 512), size - offset);
        uint16_t nlb = (uint16_t)((chunk + 511) / 512);

        void *aligned_buf;
        posix_memalign(&aligned_buf, PAGE_SIZE, nlb * 512);
        memset(aligned_buf, 0, nlb * 512);

        int ret = nvme_passthru(NVME_CMD_READ, NSID, slba, nlb, aligned_buf, nlb * 512);
        if (ret < 0) {
            free(aligned_buf);
            return ret;
        }

        memcpy(buf.data() + offset, aligned_buf, chunk);
        free(aligned_buf);

        offset += chunk;
        slba += nlb;
    }

    return 0;
}

int Embedded::Proj1::Hello() {
    return -1;
}
