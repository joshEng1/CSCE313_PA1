/*
	Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20
	
	Please include your Name, UIN, and the date below
	Name: Joshua Eng
	UIN: 334000592
	Date: 9/28/2025
*/

#include "common.h"
#include "FIFORequestChannel.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iomanip>

using namespace std;

double request_ecg(FIFORequestChannel& chan, int p, double t, int e) {
    datamsg d(p, t, e);
    chan.cwrite(&d, sizeof(d));
    double val;
    chan.cread(&val, sizeof(double));
    return val;
}

__int64_t request_file_size(FIFORequestChannel& chan, const string& fname) {
    filemsg fm(0, 0);
    int len = sizeof(filemsg) + fname.size() + 1;
    vector<char> buf(len);
    memcpy(buf.data(), &fm, sizeof(filemsg));
    strcpy(buf.data() + sizeof(filemsg), fname.c_str());

    chan.cwrite(buf.data(), len);
    __int64_t filesize;
    chan.cread(&filesize, sizeof(filesize));
    return filesize;
}

void request_file(FIFORequestChannel& chan, const string& fname, int buffercap) {
    __int64_t filesize = request_file_size(chan, fname);

    system("mkdir -p received");
    string outpath = "received/" + fname;
    FILE* out = fopen(outpath.c_str(), "wb");
    if (!out) { perror("fopen"); return; }

    __int64_t offset = 0;
    while (offset < filesize) {
        int chunk = min((__int64_t)buffercap, filesize - offset);
        filemsg fm(offset, chunk);

        int len = sizeof(filemsg) + fname.size() + 1;
        vector<char> buf(len);
        memcpy(buf.data(), &fm, sizeof(filemsg));
        strcpy(buf.data() + sizeof(filemsg), fname.c_str());

        chan.cwrite(buf.data(), len);

        vector<char> recvbuf(chunk);
        chan.cread(recvbuf.data(), chunk);
        fwrite(recvbuf.data(), 1, chunk, out);

        offset += chunk;
    }
    fclose(out);
    cout << "File saved to " << outpath << " (" << filesize << " bytes)" << endl;
}

int main (int argc, char *argv[]) {
	int opt;
	int p = -1;
	double t = -1.0;
	int e = -1;
	string filename = "";
    int buffercap = MAX_MESSAGE;
    bool newchan = false;

	while ((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1) {
		switch (opt) {
			case 'p': 
                p = atoi(optarg); 
                break;
			case 't': 
                t = atof(optarg);
                break;
			case 'e': 
                e = atoi(optarg);
                break;
			case 'f': 
                filename = optarg; 
                break;
            case 'm': 
                buffercap = atoi(optarg); 
                break;
            case 'c': 
                newchan = true; 
                break;
		}
	}

    // 🔹 Fork and exec the server
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: run server
        if (buffercap != MAX_MESSAGE) {
            string mopt = to_string(buffercap);
            execl("./server", "server", "-m", mopt.c_str(), (char*)NULL);
        } else {
            execl("./server", "server", (char*)NULL);
        }
        perror("execl failed");
        _exit(1);
    }

    // Parent (client) continues
    usleep(100000); // give server time to set up FIFOs

    // Create control channel
    FIFORequestChannel control("control", FIFORequestChannel::CLIENT_SIDE);
    FIFORequestChannel* chan = &control;

    // Optional new channel
    FIFORequestChannel* extra = nullptr;
    if (newchan) {
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        control.cwrite(&m, sizeof(m));

        char name[256];
        control.cread(name, sizeof(name));
        extra = new FIFORequestChannel(name, FIFORequestChannel::CLIENT_SIDE);
        chan = extra;
    }

    // Case 1: single datapoint
    if (p != -1 && t >= 0 && (e == 1 || e == 2)) {
        double val = request_ecg(*chan, p, t, e);
        cout << "For person " << p
        << ", at time " << fixed << setprecision(3) << t
        << ", the value of ecg " << e
        << " is " << fixed << setprecision(2) << val << endl;

    }
    // Case 2: first 1000 data points
    else if (p != -1 && t < 0 && e == -1 && filename == "") {
        system("mkdir -p received");   // ensure folder exists
        ofstream fout("received/x1.csv");
        for (int i = 0; i < 1000; i++) {
            double timepoint = i * 0.004;

            datamsg d1(p, timepoint, 1);
            chan->cwrite(&d1, sizeof(datamsg));
            double ecg1; chan->cread(&ecg1, sizeof(double));

            datamsg d2(p, timepoint, 2);
            chan->cwrite(&d2, sizeof(datamsg));
            double ecg2; chan->cread(&ecg2, sizeof(double));

            // same formatting as BIMDC CSV
            fout << setprecision(6) << timepoint << ","
            << setprecision(6) << ecg1 << ","
            << ecg2 << "\n";
        }
        fout.close();
        cout << "First 1000 points written to received/x1.csv\n";
    }
    // Case 3: file transfer
    else if (!filename.empty()) {
        request_file(*chan, filename, buffercap);
    }

    // Cleanup channels
    if (extra) {
        MESSAGE_TYPE q = QUIT_MSG;
        extra->cwrite(&q, sizeof(q));
        delete extra;
    }
    MESSAGE_TYPE q = QUIT_MSG;
    control.cwrite(&q, sizeof(q));

    // 🔹 Wait for the server to terminate
    int status;
    waitpid(pid, &status, 0);

    return 0;
}
