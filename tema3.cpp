#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

std::map<std::string, std::vector<std::string>> peer_files;
std::vector<std::map<std::string, std::vector<std::string>>> tracker_log;
std::vector<std::string> desired_files;
std::map<std::string, std::vector<int>> file_swarm;
std::map<std::string, std::vector<int>> tracker_peers;
std::vector<int> finished_download;

bool upload_loop = true;

typedef struct file_info
{
    std::vector<int> seeds;
    std::vector<std::vector<std::string>> segments;
    file_info()
    {
        segments.resize(10);
        segments.reserve(10);
    }

} file_info_t;

void parse_input(int numtasks, int rank)
{
    std::string line;
    std::ifstream input_file(std::string("in" + std::to_string(rank) + ".txt").c_str());
    getline(input_file, line);
    int no_files = std::atoi(line.c_str());
    for (int i = 0; i < no_files; i++)
    {
        getline(input_file, line);
        std::string file_name = line.substr(0, line.find(' '));

        std::string no_segs = line.substr(line.find(' ') + 1, line.size());

        for (int j = 0; j < std::atoi(no_segs.c_str()); j++)
        {
            std::string segment;
            getline(input_file, segment);

            if (peer_files.find(file_name) == peer_files.end())
            {
                std::vector<std::string> aux;
                aux.push_back(segment);
                peer_files.insert({file_name, aux});
            }
            else
            {
                peer_files[file_name].push_back(segment);
            }
        }
    }
    getline(input_file, line);
    int no_desired = std::atoi(line.c_str());

    for (int i = 0; i < no_desired; i++)
    {
        getline(input_file, line);
        desired_files.push_back(line);
    }

    input_file.close();
}

void *download_thread_func(void *arg)
{
    std::map<std::string, file_info_t> seeds;
    for (int i = 0; i < desired_files.size(); i++)
    {
        file_info_t aux;
        seeds.insert({desired_files[i], aux});
    }

    MPI_Status status;
    int rank = *(int *)arg;

    char req[100] = {0};
    std::string list_req = "list " + std::to_string(desired_files.size());
    std::memcpy(req, list_req.c_str(), list_req.size());
    MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    for (int i = 0; i < desired_files.size(); i++)
    {
        std::string file_name = desired_files[i];
        std::memset(req, 0, 100);
        std::memcpy(req, desired_files[i].c_str(), 5);
        MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        std::memset(req, 0, 100);
        int no_seeds = 0;
        MPI_Recv(&no_seeds, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);

        for (int j = 0; j < no_seeds; j++)
        {
            int seed_id = 0;
            MPI_Recv(&seed_id, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
            int no_segs = 0;
            MPI_Recv(&no_segs, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);

            for (int z = 0; z < no_segs; z++)
            {
                std::memset(req, 0, 100);
                char segment[33] = {0};
                MPI_Recv(segment, 32, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);
                seeds[file_name].segments[seed_id].push_back(std::string(segment));
            }
            seeds[file_name].seeds.push_back(seed_id);
        }
    }

    for (int i = 0; i < desired_files.size(); i++)
    {
        std::string file_name = desired_files[i];
        int seed_id = seeds[file_name].seeds[0];
        auto segments = seeds[file_name].segments[seed_id];
        int download_count = 0;

        for (int j = 0; j < segments.size(); j++)
        {
            if (download_count % 10 == 0)
            {
                std::string update_req = "update " + file_name;
                std::memset(req, 0, 100);
                std::memcpy(req, update_req.c_str(), update_req.size());

                MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
                MPI_Send(&download_count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);

                for (int z = 0; z < download_count; z++)
                {
                    std::string segment = segments[z];
                    MPI_Send(segment.c_str(), 32, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
                }
            }
            std::memset(req, 0, 100);
            std::string req_msg = "download " + file_name;
            std::memcpy(req, req_msg.c_str(), req_msg.size());
            MPI_Send(req, 100, MPI_CHAR, seed_id, 100, MPI_COMM_WORLD);
            std::string segment = segments[j];
            MPI_Send(segment.c_str(), 32, MPI_CHAR, seed_id, rank, MPI_COMM_WORLD);
            std::memset(req, 0, 100);

            MPI_Recv(req, 2, MPI_CHAR, seed_id, rank, MPI_COMM_WORLD, &status);
            download_count++;
        }
        if (download_count % 10 != 0)
        {
            std::string update_req = "update " + file_name;
            std::memset(req, 0, 100);
            std::memcpy(req, update_req.c_str(), update_req.size());
            MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            int left = segments.size() - download_count;
            MPI_Send(&left, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);

            for (int z = download_count; z < segments.size(); z++)
            {
                std::string segment = segments[z];
                MPI_Send(segment.c_str(), 32, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            }
        }

        std::string update_req = "file_done " + file_name;
        std::memset(req, 0, 100);
        std::memcpy(req, update_req.c_str(), update_req.size());
        MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

        std::string out_name = "client" + std::to_string(rank) + "_" + file_name;
        std::ofstream file(out_name);

        for (int x = 0; x < segments.size() - 1; x++)
            file << segments[x] << '\n';
        file << segments[segments.size() - 1];

        file.close();
    }

    std::string update_req = "all_files_done";
    std::memset(req, 0, 100);
    std::memcpy(req, update_req.c_str(), update_req.size());
    MPI_Send(req, 100, MPI_CHAR, 0, 0, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int *)arg;
    char msg_buf[100] = {0};
    MPI_Status status;
    while (upload_loop)
    {
        std::memset(msg_buf, 0, 100);
        MPI_Recv(msg_buf, 100, MPI_CHAR, MPI_ANY_SOURCE, 100, MPI_COMM_WORLD, &status);

        if (std::strstr(msg_buf, "download"))
        {
            std::string msg_str = std::string(msg_buf);
            std::string file_name = msg_str.substr(msg_str.find(' ') + 1, msg_str.size());
            std::memset(msg_buf, 0, 100);
            MPI_Recv(msg_buf, 32, MPI_CHAR, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD, &status);

            std::string str = "OK";
            MPI_Send(str.c_str(), 2, MPI_CHAR, status.MPI_SOURCE, status.MPI_SOURCE, MPI_COMM_WORLD);
        }
        else if (std::strstr(msg_buf, "stop"))
        {
            upload_loop = false;
        }
    }

    return NULL;
}

void tracker(int numtasks, int rank)
{
    tracker_log.resize(100);
    tracker_log.reserve(100);
    finished_download.resize(numtasks + 1);
    finished_download.reserve(numtasks + 1);

    bool loop = true;
    for (int i = 1; i < numtasks; i++)
    {
        std::string req_msg = "send_files";
        MPI_Send(req_msg.c_str(), req_msg.length(), MPI_CHAR, i, 0, MPI_COMM_WORLD);
        MPI_Status status;
        int no_files = 0;
        MPI_Recv(&no_files, 10, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);

        for (int j = 0; j < no_files; j++)
        {
            char file_name[6] = {0};
            MPI_Recv(file_name, 5, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);

            if (file_swarm.find(file_name) == file_swarm.end())
            {
                std::vector<int> aux;
                aux.push_back(i);
                file_swarm.insert({file_name, aux});
            }
            else
            {
                file_swarm[file_name].push_back(i);
            }

            int file_size = 0;
            MPI_Recv(&file_size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

            for (int z = 0; z < file_size; z++)
            {
                char segment[34] = {0};
                MPI_Recv(segment, 32, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
                tracker_log[i][file_name].push_back(segment);
            }
        }
    }
    for (int i = 1; i < numtasks; i++)
    {
        std::string req_msg = "OK";
        MPI_Send(req_msg.c_str(), req_msg.length(), MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }

    char msg_buf[100] = {0};
    while (loop)
    {
        std::memset(msg_buf, 0, 100);
        MPI_Status status = {0};
        MPI_Recv(msg_buf, 100, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        if (strstr(msg_buf, "list"))
        {
            std::string line = std::string(msg_buf);
            int no_files = std::atoi(line.substr(line.find(' ') + 1, line.size()).c_str());

            for (int j = 0; j < no_files; j++)
            {
                std::memset(msg_buf, 0, 100);
                MPI_Recv(msg_buf, 100, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
                std::string file_name = std::string(msg_buf);

                std::memset(msg_buf, 0, 100);
                int no_seeds = file_swarm[file_name].size();
                std::memcpy(msg_buf, &no_seeds, sizeof(no_seeds));
                MPI_Send(&no_seeds, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                for (int z = 0; z < no_seeds; z++)
                {
                    int seed_id = file_swarm[file_name][z];
                    std::memset(msg_buf, 0, 100);
                    std::memcpy(msg_buf, &seed_id, sizeof(seed_id));
                    MPI_Send(&seed_id, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

                    int no_segs = tracker_log[seed_id][file_name].size();
                    std::memset(msg_buf, 0, 100);
                    std::memcpy(msg_buf, &no_segs, sizeof(no_segs));
                    MPI_Send(&no_segs, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

                    for (int q = 0; q < no_segs; q++)
                    {
                        std::string segment = tracker_log[seed_id][file_name][q];
                        MPI_Send(segment.c_str(), 32, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                    }
                }
            }
        }
        else if (strstr(msg_buf, "update"))
        {
            std::string msg_str = std::string(msg_buf);
            std::string file_name = msg_str.substr(msg_str.find(' ') + 1, msg_str.size());

            auto aux_arr = file_swarm[file_name];
            if (std::find(aux_arr.begin(), aux_arr.end(), status.MPI_SOURCE) == aux_arr.end())
            {
                file_swarm[file_name].push_back(status.MPI_SOURCE);
                if (tracker_peers.find(file_name) == tracker_peers.end())
                {
                    std::vector<int> aux;
                    aux.reserve(10);
                    aux.resize(10);
                    aux[status.MPI_SOURCE] = 1;
                    tracker_peers.insert({file_name, aux});
                }
                else
                {
                    tracker_peers[file_name][status.MPI_SOURCE] = 1;
                }
            }

            int no_segs = 0;
            MPI_Recv(&no_segs, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

            for (int i = 0; i < no_segs; i++)
            {
                char seg[33] = {0};
                MPI_Recv(seg, 32, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

                auto aux_arr = tracker_log[status.MPI_SOURCE][file_name];
                if (std::find(aux_arr.begin(), aux_arr.end(), std::string(seg)) == aux_arr.end())
                {
                    tracker_log[status.MPI_SOURCE][file_name].push_back(seg);
                }
            }
        }
        else if (strstr(msg_buf, "file_done"))
        {
            std::string msg_str = std::string(msg_buf);
            std::string file_name = msg_str.substr(msg_str.find(' ') + 1, msg_str.size());
            tracker_peers[file_name][status.MPI_SOURCE] = 0;
        }
        else if (strstr(msg_buf, "all_files_done"))
        {
            finished_download[status.MPI_SOURCE] = 1;
            bool done = true;
            for (int i = 1; i < numtasks; i++)
            {
                if (finished_download[i] == 0)
                    done = false;
            }

            if (done)
            {
                std::string stop_msg = "stop";
                loop = false;
                for (int i = 1; i < numtasks; i++)
                    MPI_Send(stop_msg.c_str(), stop_msg.size(), MPI_CHAR, i, 100, MPI_COMM_WORLD);
            }
        }
    }
}

void peer(int numtasks, int rank)
{
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    parse_input(numtasks, rank);

    MPI_Status status2;
    char req_msg[11] = {0};
    MPI_Recv(req_msg, 10, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status2);
    if (std::string(req_msg) == "send_files")
    {
        int no_files = peer_files.size();
        MPI_Send(&no_files, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        auto it = peer_files.begin();
        for (int i = 0; i < no_files; i++)
        {
            std::string file_name = it->first;
            MPI_Send(file_name.c_str(), file_name.length(), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            int file_size = peer_files[file_name].size();
            MPI_Send(&file_size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);

            for (int j = 0; j < file_size; j++)
            {
                MPI_Send(peer_files[file_name][j].c_str(), peer_files[file_name][j].length(), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
            }
            it++;
        }
    }
    std::memset(req_msg, 0, 11);
    MPI_Recv(req_msg, 2, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status2);

    if (std::string(req_msg) != "OK")
        return;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK)
    {
        tracker(numtasks, rank);
    }
    else
    {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}