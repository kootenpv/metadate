

import requests

for sentence in d:
    r = requests.post("http://84.86.153.191/predict",
                      json={"sentence": sentence, "model_name": "tense"})
    print("-" * 50)
    print(sentence)
    for sentence in r.json():
        print(sentence['intent'], sentence['confidence'])


"last X years",
"X years ago",
"X years prior",
"X years earlier"
