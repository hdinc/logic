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

    // files written by uploads and drops, taken by the program
    // (src/web/web.cpp)
    Module.logicUploaded = [];
    const logicWriteAll = async (files, dir) => {
        for (const f of files) {
            try {
                Module.logicUploaded.push(await logicWrite(f, dir));
            } catch (e) {
                console.error('uploading ' + f.name + ' failed', e);
            }
        }
    };

    Module.logicUpload = dir => {
        const input = document.createElement('input');
        input.type = 'file';
        input.multiple = true;
        input.addEventListener('change', () => logicWriteAll([...input.files], dir));
        input.click();
    };

    Module.logicDownload = path => {
        const url = URL.createObjectURL(new Blob([FS.readFile(path)]));
        const a = document.createElement('a');
        a.href = url;
        a.download = path.split('/').pop();
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 60000);
    };

    // files dropped on the page go into the folder the program sets
    Module.logicDropDir = '/home/web_user/workspace';
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
        await logicWriteAll(files, Module.logicDropDir);
    });
}
