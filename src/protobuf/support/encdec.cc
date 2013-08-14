// C++ example for:
// encoding a message
// convert to text format
// parse from encoded buffer

#include <iostream>
#include <fstream>
#include <string>

#include <google/protobuf/text_format.h>
#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/value.pb.h>
#include <protobuf/generated/message.pb.h>
#include <protobuf/generated/object.pb.h>

#include <protobuf/json2pb/json2pb.h>

using namespace std;
using namespace google::protobuf;

int main(int argc, char* argv[])
{

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    string buffer;
    Container container, got;
    Telegram *telegram;
    Originator *origin;
    Object *object;
    Pin *pin;
    Value *value;

    // type-tag the container:
    container.set_type(MT_HALUPDATE);

    // prepare a submessage for reuse
    origin = new Originator();
    origin->set_origin(PROCESS);
    origin->set_name("gladevcp");
    origin->set_id(234);

    // generate a subtelegram on the fly:
    telegram = container.mutable_telegram();


    telegram->set_op(UPDATE);
    telegram->set_serial(56789);
    telegram->set_rsvp(NONE);
#if 0
    // this doesnt work with the jessie protobuf package yet
    // but not terribly important
    // plug in the prepared origin submessage
    telegram->set_allocated_origin(origin);
#endif

    // add optional submessage(s)
    object = telegram->add_args();
    object->set_type(HAL__PIN);

    pin = object->mutable_pin();
    pin->set_type(HAL__S32);
    pin->set_name("foo.1.bar");
    pin->set_hals32(4711);

    object = telegram->add_args();
    object->set_type(NAMED_VALUE);
    object->set_name("pi");

    value = object->mutable_value();
    value->set_type(DOUBLE);
    value->set_v_double(3.14159);

    // ready to serialize to wire format.
    if (argc == 1) {

	// just dump encoded string to stdout
	if (!container.SerializeToOstream(&cout)) {
	    cerr << "Failed to serialize container." << endl;
	    exit(1);
	}
    } else {
	cout << "Container converted to JSON:"  << endl;
	std::string json = pb2json(container);
	cout << json << endl;

	Container c;
	try{
	    json2pb(c, json.c_str(), strlen(json.c_str()));
	} catch (std::exception &ex) {
	    printf("json2pb exception: %s\n", ex.what());
	}
	// printf("c.DebugString=%s\n",c.DebugString().c_str());

	if (TextFormat::PrintToString(c, &buffer)) {
	    cout << "FromJson: \n" <<  buffer << endl;
	} else {
	    cerr << "Fail" << endl;
	}
	// generate external text representation
	if (TextFormat::PrintToString(container, &buffer)) {
	    cout << "Container: \n" <<  buffer << endl;
	} else {
	    cerr << "Fail" << endl;
	}

	// and convert back from wire format:
	if (!TextFormat::ParseFromString(buffer, &got)) {
	    cerr << "Failed to parse Container " << endl;
	    return -1;
	}

	if (TextFormat::PrintToString(got, &buffer)) {
	    cout << "FromWire: \n" <<  buffer << endl;
	} else {
	    cerr << "Fail" << endl;
	}
    }
    // Optional:  Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
