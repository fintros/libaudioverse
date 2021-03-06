/* Copyright 2016 Libaudioverse Developers. See the COPYRIGHT
file at the top-level directory of this distribution.

Licensed under the mozilla Public License, version 2.0 <LICENSE.MPL2 or
https://www.mozilla.org/en-US/MPL/2.0/> or the Gbnu General Public License, V3 or later
<LICENSE.GPL3 or http://www.gnu.org/licenses/>, at your option. All files in the project
carrying such notice may not be copied, modified, or distributed except according to those terms. */
#include <math.h>
#include <stdlib.h>
#include <libaudioverse/libaudioverse.h>
#include <libaudioverse/libaudioverse_properties.h>
#include <libaudioverse/nodes/fft_convolver.hpp>
#include <libaudioverse/private/node.hpp>
#include <libaudioverse/private/server.hpp>
#include <libaudioverse/private/properties.hpp>
#include <libaudioverse/private/macros.hpp>
#include <libaudioverse/private/memory.hpp>
#include <libaudioverse/private/constants.hpp>
#include <libaudioverse/private/file.hpp>
#include <libaudioverse/private/kernels.hpp>
#include <libaudioverse/implementations/convolvers.hpp>
#include <string>

namespace libaudioverse_implementation {

FftConvolverNode::FftConvolverNode(std::shared_ptr<Server> server, int channels): Node(Lav_OBJTYPE_FFT_CONVOLVER_NODE, server, channels, channels) {
	if(channels < 1) ERROR(Lav_ERROR_RANGE, "Channels must be greater than 0.");
	appendInputConnection(0, channels);
	this->channels=channels;
	appendOutputConnection(0, channels);
	convolvers=new FftConvolver*[channels]();
	for(int i= 0; i < channels; i++) convolvers[i] = new FftConvolver(server->getBlockSize());
}

std::shared_ptr<Node> createFftConvolverNode(std::shared_ptr<Server> server, int channels) {
	return standardNodeCreation<FftConvolverNode>(server, channels);
}

FftConvolverNode::~FftConvolverNode() {
	for(int i = 0; i < channels; i++) delete convolvers[i];
	delete[] convolvers;
}

void FftConvolverNode::process() {
	for(int i= 0; i < channels; i++) {
		convolvers[i]->convolve(input_buffers[i], output_buffers[i]);
	}
}

void FftConvolverNode::setResponse(int channel, int length, float* response) {
	if(channel >= channels || channel < 0) ERROR(Lav_ERROR_RANGE, "Channel out of range.");
	if(length < 1) ERROR(Lav_ERROR_RANGE, "Response must be at least one sample.");
	convolvers[channel]->setResponse(length, response);
	convolvers[channel]->reset();
}

void FftConvolverNode::setResponseFromFile(std::string path, int fileChannel, int convolverChannel) {
	if(convolverChannel < 0 || convolverChannel >= channels) ERROR(Lav_ERROR_RANGE, "Channel out of range.");
	if(fileChannel < 0) ERROR(Lav_ERROR_RANGE, "File channel must be positive.");
	FileReader reader{};
	reader.open(path.c_str());
	if(fileChannel >= reader.getChannelCount()) ERROR(Lav_ERROR_RANGE, "Channel greater than channels in file.");
	unsigned int bufferSize= reader.getSampleCount();
	float* tmp=allocArray<float>(bufferSize);
	reader.readAll(tmp);
	//This is a strange trick.  Because we only care about the specified channel, we can kill the others.
	//Consequently, we copy the channel of interest to the beginning of the buffer.
	for(int i = 0; i < reader.getFrameCount(); i++) tmp[i] = tmp[i*reader.getChannelCount()+fileChannel];
	//Resample if needed.
	float* resampledTmp;
	int resampledTmpLength;
	staticResamplerKernel(reader.getSr(), server->getSr(), 1, reader.getFrameCount(), tmp, &resampledTmpLength, &resampledTmp);
	//Finally, set the specified convolver.
	setResponse(convolverChannel, resampledTmpLength, resampledTmp);
	freeArray(tmp);
	delete[] resampledTmp;
}

//begin public api

Lav_PUBLIC_FUNCTION LavError Lav_createFftConvolverNode(LavHandle serverHandle, int channels, LavHandle* destination) {
	PUB_BEGIN
	auto server = incomingObject<Server>(serverHandle);
	LOCK(*server);
	auto retval = createFftConvolverNode(server, channels);
	*destination = outgoingObject<Node>(retval);
	PUB_END
}

Lav_PUBLIC_FUNCTION LavError Lav_fftConvolverNodeSetResponse(LavHandle nodeHandle, int channel, int length, float* response) {
	PUB_BEGIN
	auto n = incomingObject<FftConvolverNode>(nodeHandle);
	LOCK(*n);
	n->setResponse(channel, length, response);
	PUB_END
}

Lav_PUBLIC_FUNCTION LavError Lav_fftConvolverNodeSetResponseFromFile(LavHandle nodeHandle, const char* path, int fileChannel, int convolverChannel) {
	PUB_BEGIN
	auto n = incomingObject<FftConvolverNode>(nodeHandle);
	LOCK(*n);
	n->setResponseFromFile(path, fileChannel, convolverChannel);
	PUB_END
}

}