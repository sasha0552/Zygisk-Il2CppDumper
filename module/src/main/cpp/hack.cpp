//
// Created by Perfare on 2020/7/4.
//
// Modified by DJPadbit on 2024/08/02
//

#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include "And64InlineHook.hpp"
#include <cstring>
#include <string>
#include <ios>
#include <iosfwd>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <vector>

unsigned long get_module_base(const char * module_name) {
    FILE * fp;
    unsigned long addr = 0;
    char * pch;
    char filename[32] = "/proc/self/maps";
    char line[1024];
    fp = fopen(filename, "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name) && strstr(line, " r-xp")) {
                pch = strtok(line, "-");
                addr = strtoul(pch, NULL, 16);
                if (addr == 0x8000) addr = 0;
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

void hack_lib(const char * game_data_dir, std::string modName) {
    std::string outPathRo = std::string(game_data_dir).append("/files/ro_").append(modName);
    std::string outPathRw = std::string(game_data_dir).append("/files/").append(modName);

    std::string line;
    std::ifstream file("/proc/self/maps");
    if (!file.is_open()) {
        LOGI("hack_lib: couldn't open maps");
        return;
    }
    LOGI("hack_lib: map open, looking for '%s'", modName.c_str());

    struct segment {
        unsigned long start_addr;
        unsigned long size;
        unsigned long offset;
        bool writable;
    };

    std::vector<segment> segments;
    unsigned long total_size = 0;

    while (getline(file, line)) {
        if (line.find(modName) != std::string::npos) {
            segment seg;

            int pos = line.find(" ");
            std::string addr_st = line.substr(0,pos);
            line.erase(0, pos+1); // eat
            // start_addr-end_addr
            pos = addr_st.find("-");
            seg.start_addr = std::strtoul(addr_st.substr(0, pos).c_str(), NULL, 16);
            seg.size = std::strtoul(addr_st.substr(pos+1).c_str(), NULL, 16) - seg.start_addr;
            // perms
            pos = line.find(" ");
            std::string perms = line.substr(0,pos);
            line.erase(0, pos+1); // eat
            seg.writable = perms.find("w") != std::string::npos;
            // offset
            pos = line.find(" ");
            std::string offset_st = line.substr(0,pos);
            line.erase(0, pos+1); // eat

            seg.offset = std::strtoul(offset_st.c_str(), NULL, 16);

            unsigned long seg_last = seg.offset+seg.size;
            if (seg_last > total_size)
                total_size = seg_last;

            LOGI("hack_lib: seg st:%lx, sz:%lx, of:%lx, w:%i", seg.start_addr, seg.size, seg.offset, seg.writable);

            segments.push_back(seg);
        }
    }
    file.close();

    LOGI("hack_lib: tot_size: %lu", total_size);

    char * data = new char[total_size];
    if (!data) {
        LOGI("hack_lib: can't allocate memory for dump");
        return;
    }

    // Only do read-only segments first, then add potential modifications
    for (segment &seg : segments) {
        if (seg.writable)
            continue;

        LOGI("hack_lib: reading Ro segment: %lx", seg.start_addr);
        char* start = reinterpret_cast<char*>(seg.start_addr);
        std::memcpy(data+seg.offset, start, seg.size);
    }

    // Write out the Ro file
    // Not really needed
    /*LOGI("hack_lib: read all ro segs");
    std::ofstream outfileRo(outPathRo, std::ios::binary | std::ios::out);
    if (outfileRo.is_open()) {
        outfileRo.write(data, total_size);
        outfileRo.close();
    } else {
        LOGI("hack_lib: couldn't open ro outfile");
    }*/

    // Read the write segs now
    for (segment &seg : segments) {
        if (!seg.writable)
            continue;

        LOGI("hack_lib: reading Rw segment: %lx", seg.start_addr);
        char* start = reinterpret_cast<char*>(seg.start_addr);
        std::memcpy(data+seg.offset, start, seg.size);
    }

    // Write out the Rw file
    LOGI("hack_lib: read all rw segs");
    std::ofstream outfileRw(outPathRw, std::ios::binary | std::ios::out);
    if (outfileRw.is_open()) {
        outfileRw.write(data, total_size);
        outfileRw.close();
    } else {
        LOGI("hack_lib: couldn't open rw outfile");
    }

    LOGI("hack_lib: cleanup");
    delete[] data;
}

void hack_thread(const char * game_data_dir) {
    unsigned long base_addr;
    base_addr = get_module_base("libil2cpp.so");
    LOGI("hack_thread: libil2cpp.so addr %lx", base_addr);

    char * hack_addr = *(char **)(base_addr + GlobalMetadataAddr);
    unsigned long codereg_addr = *(unsigned long*)(base_addr + CodeRegAddr);
    unsigned long metareg_addr = *(unsigned long*)(base_addr + MetaRegAddr);
    unsigned long codegenopt_addr = *(unsigned long*)(base_addr + CodeGenOptAddr);

    LOGI("hack_thread: cr:%lx mr:%lx co:%lx", codereg_addr, metareg_addr, codegenopt_addr);
    LOGI("hack_thread: -base cr:%lx mr:%lx co:%lx", codereg_addr-base_addr, metareg_addr-base_addr, codegenopt_addr-base_addr);

    auto outPath = std::string(game_data_dir).append("/files/global-metadata.dat");
    std::ofstream outfile(outPath, std::ios::binary | std::ios::out);
    if (outfile.is_open()) {
        // deref once to get correct pointer to GM
        const char * variable_data = reinterpret_cast < char * > (hack_addr);
        int definitionOffset_size = *(reinterpret_cast < const int * > (variable_data + 0x108));
        int definitionsCount_size = *(reinterpret_cast < const int * > (variable_data + 0x10C));
        if (definitionsCount_size < 10) {
            definitionOffset_size = *(reinterpret_cast < const int * > (variable_data + 0x100));
            definitionsCount_size = *(reinterpret_cast < const int * > (variable_data + 0x104));
        }
        LOGI("hack_thread: defOffsetSize %x defCountSize %x", definitionOffset_size, definitionsCount_size);
        outfile.write(variable_data, definitionOffset_size + definitionsCount_size);
        outfile.close();
    }
}

struct System_String_Fields {
    int32_t length;
    uint16_t start_char;
};

struct System_String_o {
    void *klass;
    void *monitor;
    System_String_Fields fields;
};

struct Torappu_PlayerData_Fields {
    System_String_o* m_logToken;
    System_String_o* m_accessToken;
    System_String_o* m_chatMask;
    void* m_data;
    void* m_rawData;
    void* m_charIdInstMap;
    void* m_existActSet;
    void* m_existSandboxPermSet;
    void* m_jsonSettings;
    void* m_luaPlayerData;
};

struct Torappu_PlayerData_o {
    void *klass;
    void *monitor;
    Torappu_PlayerData_Fields fields;
};

System_String_o* (*old_getChatMask)(Torappu_PlayerData_o*, const void*) = nullptr;

std::string& getStringFromNet(System_String_o* in_string, std::string& out_string) {
    int32_t size = in_string->fields.length;
    uint16_t *start = &in_string->fields.start_char;

    out_string.resize(size);
    char *st = out_string.data();
    for (int i=0;i<size;i++)
        st[i] = start[i];

    return out_string;
}

System_String_o* getChatMask_hook(Torappu_PlayerData_o* __this, void* method) {
    LOGI("getChatMask_hook: hello");
    if (!old_getChatMask) {
        LOGE("getChatMask_hook: No old func ??? help");
        return nullptr;
    }
    System_String_o* ret = old_getChatMask(__this, method);
    LOGI("getChatMask_hook: Called old func, ret %lx", (unsigned long)ret);
    if (ret != nullptr) {
        std::string chatmask;
        getStringFromNet(ret, chatmask);
        LOGI("getChatMask_hook: data => '%s'", chatmask.c_str());
    }
    return ret;
}

void hack_chatmask() {
    LOGI("hack_chatmask: entry");

    if (old_getChatMask != nullptr) {
        LOGI("hack_chatmask: Already hooked ??? fuck");
        return;
    }

    unsigned long base_addr = get_module_base("libil2cpp.so");
    LOGI("hack_chatmask: libil2cpp.so addr %lx", base_addr);
    unsigned long getChatMask_addr = base_addr + GetChatMaskFuncAddr;

    A64HookFunction((void *const)getChatMask_addr, (void *)getChatMask_hook, (void **)&old_getChatMask);
    LOGI("hack_chatmask: Hooked getchatmask, res %lx", (unsigned long)old_getChatMask);
}

void hack_start(const char *game_data_dir) {
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);
            hack_thread(game_data_dir);
            hack_lib(game_data_dir, "libil2cpp.so");
            hack_lib(game_data_dir, "libanort.so");
            hack_chatmask();
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;

    void *(*loadLibrary)(const char *libpath, int flag);

    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);

    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;

    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    //TODO 等待houdini初始化
    sleep(5);

    auto libart = dlopen("libart.so", RTLD_NOW);
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                             "JNI_GetCreatedJavaVMs");
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
    JavaVM *vms_buf[1];
    JavaVM *vms;
    jsize num_vms;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }

    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif
