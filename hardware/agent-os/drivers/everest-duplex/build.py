from prepare_codecs import prepare as codecs

def prepare(context):
    return {"libraries":[str(codecs())]}
