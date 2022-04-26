TestXPlayBuf : UnitTest {
	var server;

	setUp {
		server = Server(this.class.name);
		server.bootSync;
	}

	tearDown {
		server.quit;
		server.remove;
	}

	test_dontCrashServer_whenBufIsFreed {
		var buf, synth, noServerCrash = true;
		ServerQuit.add { noServerCrash = false };
		buf = Buffer.alloc(server, server.sampleRate * 0.1);
		server.sync;
		synth = { XPlayBuf.ar(1, buf, loop:1) }.play;
		server.sync;
		buf.free;
		synth.free;
		0.1.wait;
		this.assert(noServerCrash, "should not crash the server when buf is freed while playing");
	}
}
