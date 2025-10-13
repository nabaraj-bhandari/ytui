#include "process.h"
#include "config.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <wordexp.h>

namespace proc {
    std::string exec(const std::vector<std::string>& args) {
        std::vector<const char*> c_args;
        for (const auto& arg : args) c_args.push_back(arg.c_str());
        c_args.push_back(nullptr);
        int pipefd[2];
        if (pipe(pipefd) == -1) return "pipe failed";
        pid_t pid = fork();
        if (pid == -1) return "fork failed";
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execvp(c_args[0], const_cast<char* const*>(c_args.data()));
            exit(127);
        }
        close(pipefd[1]);
        std::string result;
        char buffer[256];
        ssize_t count;
        while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            result.append(buffer, count);
        }
        close(pipefd[0]);
        waitpid(pid, nullptr, 0);
        return result;
    }

    pid_t launch_daemon(const std::vector<std::string>& args) {
        std::vector<const char*> c_args;
        for (const auto& arg : args) c_args.push_back(arg.c_str());
        c_args.push_back(nullptr);
        pid_t pid = fork();
        if (pid != 0) return pid;
        if (setsid() < 0) exit(EXIT_FAILURE);
        int dev_null = open("/dev/null", O_RDWR);
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
        execvp(c_args[0], const_cast<char* const*>(c_args.data()));
        exit(1);
    }

    void spawn_with_input(const std::string& input, const char* command) {
        wordexp_t p;
        if (wordexp(command, &p, 0) != 0) return;
        
        int pipefd[2];
        if (pipe(pipefd) == -1) { wordfree(&p); return; }
        
        pid_t pid = fork();
        if (pid == -1) { wordfree(&p); return; }

        if (pid == 0) {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            execvp(p.we_wordv[0], p.we_wordv);
            exit(127);
        }
        
        close(pipefd[0]);
        write(pipefd[1], input.c_str(), input.length());
        close(pipefd[1]);
        waitpid(pid, nullptr, 0);
        wordfree(&p);
    }

    void spawn_pager(const std::string& input) {
        spawn_with_input(input, PAGER_CMD);
    }
    
    void spawn_clipboard(const std::string& input) {
        spawn_with_input(input, CLIPBOARD_CMD);
    }

    void DownloadManager::add(const std::string& video_id, pid_t pid) {
        downloads.push_back({video_id, pid});
    }

    bool DownloadManager::is_downloading(const std::string& video_id) {
        return std::any_of(downloads.begin(), downloads.end(), 
            [&](const auto& d){ return d.video_id == video_id; });
    }

    void DownloadManager::check_finished() {
        if (downloads.empty()) return;
        downloads.erase(std::remove_if(downloads.begin(), downloads.end(),
            [](const auto& d){
                int status;
                return waitpid(d.pid, &status, WNOHANG) != 0;
            }), downloads.end());
    }
}
