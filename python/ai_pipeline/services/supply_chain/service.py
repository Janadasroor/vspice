import os
import json
import requests
from typing import Dict, Any, List, Optional

class SupplyChainService:
    """
    Service to fetch real-time component data from Octopart or other supply chain APIs.
    If no API key is provided, it falls back to simulated/cached data.
    """
    
    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key
        self.base_url = "https://octopart.com/api/v4/endpoint" # Placeholder for actual v4 endpoint

    def search_component(self, query: str) -> List[Dict[str, Any]]:
        """Search for a component and return basic info, price, and stock."""
        if not self.api_key:
            return self._get_simulated_data(query)
            
        try:
            # Actual Octopart GraphQL or REST call would go here
            # For now, we simulate the structure of a real response
            headers = {"Authorization": f"Bearer {self.api_key}"}
            params = {"q": query, "limit": 5}
            # response = requests.get(self.base_url, headers=headers, params=params)
            # return response.json()
            return self._get_simulated_data(query) # Fallback while developing
        except Exception as e:
            print(f"SupplyChainService Error: {e}")
            return []

    def _get_simulated_data(self, query: str) -> List[Dict[str, Any]]:
        """Simulated data for development when no API key is present."""
        q = query.lower()
        if "tl072" in q:
            return [{
                "mpn": "TL072IP",
                "manufacturer": "Texas Instruments",
                "description": "Low-Noise JFET-Input Operational Amplifier",
                "price": 0.45,
                "currency": "USD",
                "stock": 14500,
                "distributor": "DigiKey",
                "datasheet_url": "https://www.ti.com/lit/ds/symlink/tl072.pdf"
            }]
        elif "2n3904" in q:
            return [{
                "mpn": "2N3904BU",
                "manufacturer": "onsemi",
                "description": "NPN General Purpose Transistor",
                "price": 0.08,
                "currency": "USD",
                "stock": 82000,
                "distributor": "Mouser",
                "datasheet_url": "https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf"
            }]
        
        return [{
            "mpn": f"{query.upper()}-GENERIC",
            "manufacturer": "Generic Vendor",
            "description": f"Standard {query} component",
            "price": 1.0,
            "currency": "USD",
            "stock": 100,
            "distributor": "GenericDist",
            "datasheet_url": ""
        }]
