#!/bin/bash

PROTO_OUT_DIR=proto
LIVEKIT_PROTO_DIR=external/livekit-protocol/protobufs
APP_PROTO_DIR=external/protocol/protos

# only DataPacket is used from livekit, so generate it and its imports instead of
# the whole protobufs tree (parts of which need psrpc include paths we do not have)
LIVEKIT_PROTOS=(
  livekit_models.proto
  livekit_metrics.proto
  logger/options.proto
)

mkdir -p $PROTO_OUT_DIR

FILES=""
for proto in "${LIVEKIT_PROTOS[@]}"; do
  FILES="$FILES $LIVEKIT_PROTO_DIR/$proto"
done
FILES="$FILES $APP_PROTO_DIR/*.proto"

protoc -I=$LIVEKIT_PROTO_DIR -I=$APP_PROTO_DIR --cpp_out=lite:$PROTO_OUT_DIR $FILES
