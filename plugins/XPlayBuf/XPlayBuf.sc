XPlayBuf : MultiOutUGen {
	*ar { arg numChannels, bufnum=0, rate=1.0, trigger=1.0, startPos=0.0, loopDur(-1), loop = 0, fadeTime=0.001, equalPower=1, interpolation=2;
		^this.multiNew('audio', numChannels, bufnum, rate, trigger, startPos, loopDur, loop, fadeTime, equalPower, interpolation)
	}

	init { arg argNumChannels ... theInputs;
		inputs = theInputs;
		^this.initOutputs(argNumChannels, rate);
	}

	argNamesInputsOffset { ^2 }

	checkInputs {
		/* TODO */
		^this.checkValidInputs;
	}
}
