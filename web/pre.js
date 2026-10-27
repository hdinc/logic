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

    // a file from the user's computer into the file system, written like
    // any other file so autoPersist keeps it. the path once it's written.
    const logicWrite = async (file, dir) => {
        const path = dir.replace(/\/$/, '') + '/' + file.name;
        FS.mkdirTree(dir);
        FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));
        return path;
    };

    // uploads by the program (src/web/web.cpp): the path by caller id
    Module.logicUploads = {};
    Module.logicUpload = (id, dir) => {
        const input = document.createElement('input');
        input.type = 'file';
        input.addEventListener('change', async () => {
            if (!input.files.length) return;
            try {
                Module.logicUploads[id] = await logicWrite(input.files[0], dir);
            } catch (e) {
                console.error('uploading ' + input.files[0].name + ' failed', e);
            }
        });
        input.click();
    };

    // files dropped on the page go into the folder the program sets
    Module.logicDropDir = '/home/web_user/workspace';
    Module.logicDropped = [];
    window.addEventListener('dragover', e => {
        if (e.dataTransfer.types.includes('Files')) {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'copy';
        }
    });
    window.addEventListener('drop', async e => {
        // the list empties once the handler returns, before the awaits
        const files = [...e.dataTransfer.files];
        if (!files.length) return;
        e.preventDefault();
        for (const f of files) {
            try {
                Module.logicDropped.push(await logicWrite(f, Module.logicDropDir));
            } catch (err) {
                console.error('uploading ' + f.name + ' failed', err);
            }
        }
    });
}
