// before main the home folder, with the workspace and imgui.ini, is read
// back from the browser's IndexedDB. changes are written back as files
// close (autoPersist).
if (!ENVIRONMENT_IS_PTHREAD) {
    Module.preRun = [...(Module.preRun || []), () => {
        FS.mount(IDBFS, { autoPersist: true }, '/home/web_user');
        addRunDependency('idbfs');
        FS.syncfs(true, err => {
            if (err) console.error('reading the saved workspace failed', err);
            removeRunDependency('idbfs');
        });
    }];

    // the program sets logicUnsaved each frame
    window.addEventListener('beforeunload', e => {
        if (Module.logicUnsaved) e.preventDefault();
    });
}
