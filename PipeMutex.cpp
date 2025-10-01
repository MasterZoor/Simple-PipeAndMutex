#include <windows.h>
#include <iostream>
#include <thread>
#include <random>
#include <chrono>

struct SharedData {
    int sameCount;
};

const char* SHARED_MEM_NAME = "Local\\MySharedMemory";
const char* MUTEX_NAME = "Local\\MySharedMutex";
const char* PIPE_NAME = "\\\\.\\pipe\\DynVarPipe";


void pipeThreadFunc(int& dynVar, int& otherDynVar, bool& connected) {
    while (true) {
        // Try to open the pipe (client)
        HANDLE hPipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            // Pipe not found -> create as server
            hPipe = CreateNamedPipeA(
                PIPE_NAME,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1,
                sizeof(int),
                sizeof(int),
                0,
                NULL
            );
            if (hPipe == INVALID_HANDLE_VALUE) {
                std::cerr << "Failed to create pipe: " << GetLastError() << std::endl;
                return;
            }

            std::cout << "Waiting for another process..." << std::endl;
            ConnectNamedPipe(hPipe, NULL);
            connected = true;
        }
        else {
            connected = true; // Successfully connected as client
        }

        // Communication loop
        while (true) {
            DWORD bytesWritten = 0;
            DWORD bytesRead = 0;

            
            if (!WriteFile(hPipe, &dynVar, sizeof(dynVar), &bytesWritten, NULL)) break;

            
            int received = -1;
            if (!ReadFile(hPipe, &received, sizeof(received), &bytesRead, NULL)) break;

            otherDynVar = received;

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        CloseHandle(hPipe);
        connected = false;

        // Retry connection after brief wait
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 20);

    int dynVar = dist(gen);
    int otherDynVar = -1;
    bool connected = false;

    std::cout << "Process " << GetCurrentProcessId() << " initial dynVar: " << dynVar << std::endl;

    
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sizeof(SharedData),
        SHARED_MEM_NAME
    );
    if (!hMapFile) {
        std::cerr << "CreateFileMapping failed: " << GetLastError() << std::endl;
        return 1;
    }

    SharedData* sharedData = (SharedData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!sharedData) {
        std::cerr << "MapViewOfFile failed: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        return 1;
    }

    if (sharedData->sameCount == 0) sharedData->sameCount = 0;

    // Mutex setup
    HANDLE hMutex = CreateMutexA(NULL, FALSE, MUTEX_NAME);
    if (!hMutex) {
        std::cerr << "CreateMutex failed: " << GetLastError() << std::endl;
        UnmapViewOfFile(sharedData);
        CloseHandle(hMapFile);
        return 1;
    }

    // Start pipe communication thread
    std::thread pipeThread(pipeThreadFunc, std::ref(dynVar), std::ref(otherDynVar), std::ref(connected));

    
    for (int i = 0; i < 100; i++) {
        dynVar = dist(gen);

        if (connected && otherDynVar != -1) {
            if (WaitForSingleObject(hMutex, INFINITE) == WAIT_OBJECT_0) {
                if (dynVar == otherDynVar) sharedData->sameCount++;
                ReleaseMutex(hMutex);
            }
        }

        std::cout << "DynVar: " << dynVar
            << ", OtherDynVar: " << otherDynVar
            << ", SameCount: " << sharedData->sameCount << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    pipeThread.detach();

    CloseHandle(hMutex);
    UnmapViewOfFile(sharedData);
    CloseHandle(hMapFile);
    system("pause");

    return 0;
}
