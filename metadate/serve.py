# optional flask dependency
from flask import Flask
from flask import request
from flask import jsonify
from metadate import parse_date

app = Flask(__name__)


@app.route("/metadate", methods=["GET", "POST"])
def root():
    text = request.args.get("text")
    print(text)
    future = request.args.get("future")
    future = future.lower() == 'true' if future else True
    res = parse_date(text, future)
    if res:
        res = res.to_dict()
    return jsonify(res)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8002)
