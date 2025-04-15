from flask import Flask, request, jsonify
import pandas as pd
import joblib
from flask_cors import CORS
import json
import logging
import os

app = Flask(__name__)
CORS(app)

# Configure logging
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger(__name__)

# Load model artifacts
try:
    model = joblib.load("water_requirement_model.joblib")
    encoder = joblib.load("target_encoder.joblib")
    with open("metadata.json") as f:
        metadata = json.load(f)
    logger.info("Model artifacts loaded successfully")
    logger.debug(f"Model expects {len(model.feature_names_in_)} features")
except Exception as e:
    logger.error(f"Failed to load model: {str(e)}")
    raise

@app.route("/predict", methods=["POST", "OPTIONS"])
def predict():
    if request.method == "OPTIONS":
        return jsonify({"status": "success"}), 200
    
    try:
        # Get and validate input data
        data = request.get_json()
        logger.debug(f"Raw input data: {data}")

        # Transform frontend data to model format
        input_data = {
            "Crop Name": str(data.get("crop_planted", "Corn")).title(),
            "Soil Type": str(data.get("soil_type", "Red Soil")).title(),
            "Soil Moisture (%)": float(data.get("soil_moisture", 0)),
            "Temp (°C)": float(data.get("temperature", 25)),
            "Humidity (%)": float(data.get("humidity", 50)),
            "Water Level (%)": float(data.get("water_level", 0)),
            "Crop Age (days)": int(data.get("age_of_crop", 1)),
            "Dummy Feature": 0  # Add missing feature
        }

        # Create DataFrame with all expected features
        input_df = pd.DataFrame([input_data])
        logger.debug(f"Formatted input: {input_df.to_dict()}")

        # Apply target encoding
        encoded_df = encoder.transform(input_df)
        logger.debug(f"Encoded features shape: {encoded_df.shape}")

        # Ensure all expected features are present
        missing_features = set(model.feature_names_in_) - set(encoded_df.columns)
        if missing_features:
            logger.warning(f"Adding missing features: {missing_features}")
            for feature in missing_features:
                encoded_df[feature] = 0

        # Reorder columns to match model expectations
        encoded_df = encoded_df[model.feature_names_in_]
        logger.debug(f"Final features: {encoded_df.columns.tolist()}")

        # Predict
        water_ml = float(model.predict(encoded_df)[0])

        # Apply business rule override
        if input_df["Water Level (%)"].iloc[0] > 0 and input_df["Crop Name"].iloc[0] != "Rice":
            water_ml = 0
            logger.info("Applied water level override")

        return jsonify({
            "water_required": round(water_ml, 2),
            "status": "success",
            "message": "Prediction successful",
            "features_used": input_df.iloc[0].to_dict()
        })

    except Exception as e:
        logger.error(f"Prediction error: {str(e)}", exc_info=True)
        return jsonify({
            "error": str(e),
            "status": "error",
            "message": "Prediction failed"
        }), 400

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
