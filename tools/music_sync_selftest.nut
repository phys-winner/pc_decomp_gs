// Append temporarily after common.nut in a diagnostic build; run the production wrapper in the game VM.
local musicRoot = getroottable();
local savedMusicName = musicRoot.GetBgmName, savedMusicSync = musicRoot.PortPlayBgmSync;
local musicState = { calls = [], name = "" };
try {
    musicRoot.GetBgmName = function() : (musicState) { return musicState.name; };
    musicRoot.PortPlayBgmSync = function(name, fade, volume) : (musicState) {
        musicState.calls.append([name, fade, volume]);
        musicState.name = name;
    };
    musicRoot.PlayBgmSync("data/bgm/mdk_s5.ogg", 3000, 100);
    assert(musicState.calls.len() == 1);
    assert(musicState.calls[0][0] == "data/bgm/mdk_s5.ogg" && musicState.calls[0][1] == 3000 && musicState.calls[0][2] == 100);
    for (local request = 0; request < 60; ++request)
        musicRoot.PlayBgmSync("data/bgm/mdk_s5.ogg", 3000, 100);
    assert(musicState.calls.len() == 1);
    musicRoot.PlayBgmSync("data/bgm/mdk_s6.ogg", 0, 50);
    assert(musicState.calls.len() == 2 && musicState.calls[1][1] == 0 && musicState.calls[1][2] == 50);
    musicState.name = "";
    musicRoot.PlayBgmSync("data/bgm/mdk_s6.ogg", 500, 75);
    assert(musicState.calls.len() == 3 && musicState.calls[2][1] == 500 && musicState.calls[2][2] == 75);
} catch (error) {
    musicRoot.GetBgmName = savedMusicName;
    musicRoot.PortPlayBgmSync = savedMusicSync;
    throw error;
}
musicRoot.GetBgmName = savedMusicName;
musicRoot.PortPlayBgmSync = savedMusicSync;
::PortMenu(3, "PASS: music sync forwards parameters, skips 60 duplicate requests, changes tracks and restarts after stop");
