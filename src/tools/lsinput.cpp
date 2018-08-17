/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * lsinput, list input devices from /dev/input/event                                 *
 *                                                                                   *
 * Copyright (C) 2018 Jonas Møller (no) <jonasmo441@gmail.com>                       *
 * All rights reserved.                                                              *
 *                                                                                   *
 * Redistribution and use in source and binary forms, with or without                *
 * modification, are permitted provided that the following conditions are met:       *
 *                                                                                   *
 * 1. Redistributions of source code must retain the above copyright notice, this    *
 *    list of conditions and the following disclaimer.                               *
 * 2. Redistributions in binary form must reproduce the above copyright notice,      *
 *    this list of conditions and the following disclaimer in the documentation      *
 *    and/or other materials provided with the distribution.                         *
 *                                                                                   *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND   *
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED     *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE            *
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE      *
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR        *
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER        *
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,     *
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/** @file */

#include <string>
#include <sstream>
#include <iostream>
#include <memory>
#include <vector>

extern "C" {
    #include <unistd.h>
    #include <fcntl.h>
    #include <dirent.h>
    #include <linux/input.h>
    #include <linux/uinput.h>
    #include <sys/stat.h>
    #include <stdlib.h>
}

#include "SystemError.hpp"
#include "utils.hpp"

using namespace std;

/**
 * Find symbolic links to a target inode from within a directory.
 *
 * @param target_rel Path to the target.
 * @param dirpath Path to the directory to search for links in.
 * @return Pointer to vector of paths to symbolic links that
 *         reference `target_rel`. Must be deleted by the caller.
 */
vector<string> *getLinksTo(const string& target_rel, const string& dirpath) {
    using VecT = unique_ptr<vector<string>>;
    DIR *dir = opendir(dirpath.c_str());
    if (dir == nullptr)
        throw SystemError("Unable to open directory: ", errno);
    char *target_real_c = realpath(target_rel.c_str(), nullptr);
    if (target_real_c == nullptr)
        throw SystemError("Failure in realpath(): ", errno);
    string target(target_real_c);
    free(target_real_c);

    VecT vec = VecT(new vector<string>);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        string path = dirpath + "/" + string(entry->d_name);
        struct stat stbuf;
        // Use lstat, as it won't silently look up symbolic links.
        if (lstat(path.c_str(), &stbuf) == -1)
            throw SystemError("Failure in stat(): ", errno);

        if (S_ISLNK(stbuf.st_mode)) {
            char lnk_rel_c[PATH_MAX];
            // Get link contents
            ssize_t len = readlink(path.c_str(), lnk_rel_c, sizeof(lnk_rel_c));
            if (len == -1)
                throw SystemError("Failure in readlink(): ", errno);
            string lnk_rel(lnk_rel_c, len);
            // lnk_rel path may only be valid from within the directory.
            ChDir cd(dirpath);
            char *lnk_dst_real = realpath(lnk_rel.c_str(), nullptr);
            cd.popd(); // May throw SystemError
            if (lnk_dst_real == nullptr)
                throw SystemError("Failure in realpath(): ", errno);
            string lnk_dst(lnk_dst_real);
            free(lnk_dst_real);
            if (target == lnk_dst)
                vec->push_back(string(path));
        }
    }

    return vec.release();
}

static inline void printLinks(const string& path, const string& dir) {
    string dir_base = pathBasename(dir);
    try {
        auto links = mkuniq(getLinksTo(path, dir));
        for (const auto& lnk : *links)
            cout << "    " << dir_base << ": " << pathBasename(lnk) << endl;
    } catch (const SystemError &e) {
        cout << "    " << dir_base << ": " << "Unable to acquire links: " << e.what();
    }
}

int main(int argc, char *argv[]) {
    char buf[256];
    int c;

    while ((c = getopt(argc, argv, "hv")) != -1)
        switch (c) {
            case 'h':
                cout <<
                    "lsinput:" << endl <<
                    "    List all input devices from /dev/input/event*" << endl <<
                    "    Display their names, ids, and paths." << endl <<
                    "Usage:" << endl <<
                    "    lsinput [-hv]" << endl;
                return EXIT_SUCCESS;
            case 'v':
                printf("lsinput v0.1\n");
                return EXIT_SUCCESS;
        }

    string devdir = "/dev/input";
    DIR *dir = opendir(devdir.c_str());
    if (dir == nullptr) {
        cout << "Unable to open /dev/input directory" << endl;
        return EXIT_FAILURE;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        int fd, ret;
        string filename(entry->d_name);
        try {
            if (filename.substr(0, 5) != "event")
                continue;
        } catch (out_of_range &e) {
            continue;
        }
        string path = devdir + "/" + filename;
        fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;

        ret = ioctl(fd, EVIOCGNAME(sizeof(buf)), buf);
        string name(ret > 0 ? buf : "unknown");
        cout << pathBasename(path.c_str()) << ": " << name << endl;

        printLinks(path, "/dev/input/by-path");
        printLinks(path, "/dev/input/by-id");

        close(fd);
    }

    return 0;
}