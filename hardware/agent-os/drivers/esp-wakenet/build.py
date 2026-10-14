from prepare_speech import prepare as prepare_speech

def prepare(context):
    return {"libraries": [str(prepare_speech(context["port"]["target"]))]}
