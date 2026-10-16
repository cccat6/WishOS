from prepare_nimble_memory import prepare as memory

def prepare(context):
    return {"libraries":[str(memory())]}
