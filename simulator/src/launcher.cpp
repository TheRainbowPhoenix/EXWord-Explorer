/* NCInput-style simulator launcher.
 * Scripted keys traverse the real C Explorer UI and capture its actual VRAM.
 * Keyboard/touch input can also be used interactively with --interactive. */
#include <gint/keyboard.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" void simulator_init(void);
extern "C" int simulator_capture_bmp(const char *);
extern "C" int explorer_app_main(void);

static std::string output_directory;
static size_t cursor;

struct Step { int key; const char *capture_before; };
static const Step scenario[] = {
    {KEY_EXE, "home"},
    {KEY_EXE, "directory"},
    {KEY_EX_AUDIO, "hex-viewer"},
    {KEY_EXIT, "hex-options"},
    {KEY_EXIT, nullptr},            // leave the hex viewer
    {KEY_DOWN, nullptr},            // select sample.cjf
    {KEY_EXE, nullptr},
    {KEY_EX_AUDIO, nullptr},
    {KEY_DOWN, nullptr},            // select the font option
    {KEY_EXE, nullptr},
    {KEY_EXIT, "font-grid"},
    {KEY_EXIT, nullptr},            // leave hex view for directory
    {KEY_DOWN, nullptr},            // select colors.bin
    {KEY_EXE, nullptr},
    {KEY_EX_AUDIO, nullptr},
    {KEY_EXE, nullptr},             // bitmap preview
    {KEY_EXIT, "bitmap"},
    {KEY_EXIT, nullptr},            // hex -> directory
    {KEY_EXIT, nullptr},            // directory -> volumes
    {KEY_DOWN, nullptr},{KEY_DOWN, nullptr},
    {KEY_DOWN, nullptr},{KEY_DOWN, nullptr},
    {KEY_DOWN, nullptr},{KEY_DOWN, nullptr},
    {KEY_DOWN, nullptr},{KEY_DOWN, nullptr},
    {KEY_EXE, nullptr},             // mock USB dashboard captures itself
    {KEY_ACON, nullptr}
};
extern "C" int sim_script_active(void)
{
    return !output_directory.empty();
}
extern "C" void sim_capture_named(const char *name)
{
    if(output_directory.empty()) return;
    std::string path=output_directory+"/"+name+".bmp";
    if(simulator_capture_bmp(path.c_str())!=0) {
        std::fprintf(stderr,"Could not capture %s\n",path.c_str());
        std::exit(2);
    }
    std::printf("SCREENSHOT=%s\n",path.c_str());
    std::fflush(stdout);
}
extern "C" key_event_t sim_script_next(void)
{
    if(cursor>=sizeof scenario/sizeof *scenario)
        return {KEYEV_DOWN, KEY_ACON, 0,0,0,0};
    Step const &step=scenario[cursor++];
    if(step.capture_before) sim_capture_named(step.capture_before);
    return {KEYEV_DOWN, step.key, 0,0,0,0};
}
int main(int argc,char **argv)
{
    if(argc==3 && !std::strcmp(argv[1],"--screenshots")) {
        output_directory=argv[2];
        std::filesystem::create_directories(output_directory);
    }
    else if(argc>1 && std::strcmp(argv[1],"--interactive")) {
        std::fprintf(stderr,"Usage: %s [--screenshots DIRECTORY|--interactive]\n",argv[0]);
        return 2;
    }
    simulator_init();
    int rc=explorer_app_main();
    if(!output_directory.empty()) {
        static const char *required[]={
            "home","directory","hex-viewer","hex-options",
            "font-grid","bitmap","usb-remote"
        };
        for(const char *name:required) {
            if(!std::filesystem::exists(output_directory+"/"+name+".bmp")) {
                std::fprintf(stderr,"Missing screenshot: %s\n",name);
                return 3;
            }
        }
    }
    return rc==-2 ? 0 : rc;
}
