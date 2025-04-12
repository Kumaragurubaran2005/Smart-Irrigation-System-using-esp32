from flask import Flask, request, jsonify
import pandas as pd
import joblib
from flask_cors import CORS
import requests
import numpy as np
import logging

app = Flask(__name__)
CORS(app)

# Configure logging
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger(__name__)

# Load the trained model
model = joblib.load("lightgbm_model.pkl")

# Define all expected features
ALL_FEATURES = [
    'Age of Crop (days)',
    'Rainfall (mm)',
    'Water Level (cm)',
    'Humidity (%)',
    'Temperature (°C)',
    'Soil Moisture (%)',
    'Soil Type_Black Soil',
    'Soil Type_Clay Soil',
    'Soil Type_Red Soil',
    'Soil Type_Sandy Soil',
    'Crop Planted_Maize',
    'Crop Planted_Peanuts',
    'Crop Planted_Rice',
    'Crop Planted_Wheat'
]

def get_precipitation(api_key, city):
    """Fetch precipitation data from OpenWeatherMap"""
    try:
        url = f"http://api.openweathermap.org/data/2.5/weather?q={city}&appid={api_key}&units=metric"
        response = requests.get(url)
        response.raise_for_status()
        weather_data = response.json()
        rainfall = round(float(weather_data.get("rain", {}).get("1h", 0.0)), 1)
        logger.debug(f"Rainfall data for {city}: {rainfall} mm")
        return rainfall
    except Exception as e:
        logger.error(f"Weather API error: {str(e)}")
        return 0.0

@app.route("/predict", methods=["POST", "OPTIONS"])
def predict():
    if request.method == "OPTIONS":
        # Handle CORS preflight
        return jsonify({"status": "success"}), 200
    
    try:
        # Get and validate input data
        data = request.get_json()
        logger.debug(f"Received request data: {data}")

        if not data:
            return jsonify({
                "error": "No data provided in request",
                "status": "error"
            }), 400
        
        # Required fields from frontend
        required_fields = {
            "humidity": "Humidity (%)",
            "temperature": "Temperature (°C)",
            "soil_moisture": "Soil Moisture (%)",
            "water_level": "Water Level (cm)",
            "age_of_crop": "Age of Crop (days)",
            "soil_type": "Soil Type",
            "crop_planted": "Crop Planted"
        }

        # Check for missing required fields
        missing_fields = [field for field in required_fields.keys() if field not in data]
        if missing_fields:
            error_msg = f"Missing required fields: {missing_fields}"
            logger.error(error_msg)
            return jsonify({
                "error": error_msg,
                "status": "error"
            }), 400

        # Build complete feature dictionary
        features = {
            "Humidity (%)": round(float(data["humidity"]), 1),
            "Temperature (°C)": round(float(data["temperature"]), 1),
            "Soil Moisture (%)": round(float(data["soil_moisture"]), 1),
            "Water Level (cm)": round(float(data["water_level"]), 1),
            "Age of Crop (days)": int(data["age_of_crop"]),
            "Rainfall (mm)": round(get_precipitation("b7da58af55ef8487c781e04a2b072403", data.get("city", "Vellore")), 1),
            "Soil Type": data["soil_type"],
            "Crop Planted": data["crop_planted"]
        }

        logger.debug(f"Features before encoding: {features}")

        # Create DataFrame
        df = pd.DataFrame([features])

        # Apply one-hot encoding to categorical features
        df_encoded = pd.get_dummies(df, columns=["Soil Type", "Crop Planted"], dtype=int, drop_first=False)

        # Ensure all expected columns are present with correct values
        for feature in ALL_FEATURES:
            if feature not in df_encoded:
                df_encoded[feature] = 0

        # Select only the expected columns in the correct order
        df_encoded = df_encoded[ALL_FEATURES]

        # Make prediction
        prediction = model.predict(df_encoded)
        water_required = float(prediction[0]) if prediction.size > 0 else 0.0

        logger.info(f"Predicted Water Requirement: {water_required:.2f} liters")

        # Prepare response
        return jsonify({
            "water_required": water_required,
            "status": "success",
            "message": "Prediction successful",
            "features_used": features
        })

    except Exception as e:
        logger.error(f"Prediction error: {str(e)}", exc_info=True)
        return jsonify({
            "status": "error",
            "message": str(e),
            "water_required": 0.0
        }), 500

if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)