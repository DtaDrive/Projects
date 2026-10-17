package quality;

import java.util.ArrayList;

/**
 * This class represents the AirQuality system which populates a hashtable with states and counties 
 * and calculates various statistics related to air quality.
 *
 */

public class AirQuality {

    private State[] states; // hash table used to store states. This HT won't need rehashing.

    /**
     * Constructor creates a table of size 10.
     */

    public AirQuality () {
        states  = new State[10];
    }     

    /**
     * Returns the hash table.
     * @return the value held to represent the hash table
     */
    public State[] getHashTable() {
        return states;
    }
    
    /**
     * This method populates the hashtable with the information from the inputFile parameter.
     * It is expected to insert a state and then the counties into each state.
     */

    public void buildTable ( String inputFile ) {
        
        StdIn.setFile(inputFile); // opens the inputFile to be read
        StdIn.readLine();         // skips header
        
        while ( !StdIn.isEmpty() ) {

            String line = StdIn.readLine(); 
            State s = addState( line );
            addCountyAndPollutant(s, line );
        }
    }
    
    /**
     * Inserts a single State into the hash table states.
     * return the State object if already present in the table or the newly created
     * State object inserted.
     */

    public State addState ( String inputLine ) {
        String[] token = inputLine.split(",");
        String stateName = token[0];
        String countyName = token[1];
        int aqi = Integer.parseInt(token[2]);
        int key = Math.abs(stateName.hashCode())%states.length;
        State newState = new State(stateName);
        State ptr = states[key];
        if(ptr == null){
            states[key] = newState;
            return states[key];
        } else{
            while (ptr!=null){
                if (ptr.getName().equals(stateName)) return ptr;
                ptr = ptr.getNext();
            }
            newState.setNext(states[key]);
            states[key] = newState;
            return states[key];
        }

    }
    
    /**
     * Returns true if the counties hash table (within State) needs to be resized (re-hashed)
     * Resize the hash table when (number of counties)/(array size) >= loadFactor
     *return true if resizing needs to happen, false otherwise
     */

     public boolean checkCountiesHTLoadFactor ( State state ) {

         int countiesLength = state.getCounties().length;
         int numCounties = state.getNumberOfCounties();
         int loadFactor = state.getLoadFactor();

         if (numCounties/countiesLength >= loadFactor) return true;
         else return false;
     }

    /**
     * Resizes (rehashes) the State's counties hashtable by doubling its size.
     */
    public void rehash ( State state ) {
        County[] newCounties = new County[state.getCounties().length*2];
        County[] oldCounties = state.getCounties();
        for (int i = 0; i<state.getCounties().length; i++){
            County ptr = oldCounties[i];
            while (ptr!=null){
                String countyName = ptr.getName();
                int key = Math.abs(countyName.hashCode())%newCounties.length;
                County nextCounty = ptr.getNext();
                ptr.setNext(newCounties[key]);
                newCounties[key] = ptr;
                ptr = nextCounty;
            }

        }
        state.setCounties(newCounties);
	
    }

    /**
     * Inserts the county into State if not already present.
     * Check the State's counties hash table load factor after inserting.
     * 
     * Then inserts the pollutant into County if not already present.
     * If pollutant is present, update AQI.
     */

    public void addCountyAndPollutant ( State state, String inputLine ) {
        String[] token = inputLine.split(",");
        String countyName = token[1];
        int aqi = Integer.parseInt(token[2]);
        double latitude = Double.parseDouble(token[3]);
        double longitude = Double.parseDouble(token[4]);
        String pollutant = token[5];
        String color = token[6];

        County[] counties = state.getCounties();
        int key = Math.abs(countyName.hashCode())%counties.length;
        County ptr = counties[key];
        while(ptr!=null){
            if (ptr.getName().equals(countyName)){
                boolean containsPollutant = false;
                ArrayList<Pollutant> polList = ptr.getPollutants();
                for (Pollutant p: polList){
                    if(p.getName().equals(pollutant)){
                        containsPollutant = true;
                        p.setAQI(aqi);
                        p.setColor(color);
                    }
                }
                if (!containsPollutant){
                    polList.add(new Pollutant(pollutant,aqi,color));
                }
                return;
            }
            ptr = ptr.getNext();
        }

        state.addCounty(new County(countyName, latitude, longitude, counties[key]));
        if(checkCountiesHTLoadFactor(state)){
            rehash(state);
            counties = state.getCounties();
            key = Math.abs(countyName.hashCode())%counties.length;
        }

        ptr = counties[key];
        while(!ptr.getName().equals(countyName)){
            ptr = ptr.getNext();
        }
        ptr.getPollutants().add(new Pollutant(pollutant, aqi, color));

    }

    /**
     * Sets states' simple stats AQI for each State in the hash table.
     */
    public void setStatesAQIStats() {
        for (int i = 0; i<states.length; i++){
            State ptr = states[i];

            while(ptr!=null){
                County[] counties = ptr.getCounties();
                double totalAQI = 0.0;
                County highestAQI = null;
                int highestAQInum = -999999999;
                County lowestAQI = null;
                int lowestAQInum = 999999999;
                int numPollutants = 0;

                for (int k = 0; k<counties.length;k++){
                    County ptr2 = counties[k];
                    while(ptr2!=null){
                        int countyAQI = 0;

                        for (Pollutant p : ptr2.getPollutants()){
                            countyAQI += p.getAQI();
                            numPollutants++;
                        }
                        totalAQI+=countyAQI;
                        if (countyAQI>highestAQInum){
                            highestAQInum = countyAQI;
                            highestAQI = ptr2;
                        }
                        if (countyAQI<lowestAQInum){
                            lowestAQInum = countyAQI;
                            lowestAQI = ptr2;
                        }
                        ptr2 = ptr2.getNext();
                    }
                }
                ptr.setAvgAQI(totalAQI/numPollutants);
                ptr.setLowestAQI(lowestAQI);
                ptr.setHighestAQI(highestAQI);

                ptr = ptr.getNext();
            }
        }


    }

    /**
     * Return list of counties that meet the criteria
     */

    public ArrayList<County> meetsThreshold(String stateName, String pollutantName, int AQIThreshold) {

        ArrayList<County> Threshold = new ArrayList<County>();
        for(int i = 0; i<states.length;i++){
            State ptr = states[i];
            while(ptr!=null){
                if(ptr.getName().equals(stateName)){
                    County[] counties = ptr.getCounties();
                    for (int k = 0; k<counties.length;k++){
                        County ptr2 = counties[k];
                        while(ptr2!=null){
                            ArrayList<Pollutant> polList = ptr2.getPollutants();
                            for(int j = 0; j<polList.size(); j++){
                                if(polList.get(j).getName().equals(pollutantName) && polList.get(j).getAQI()>=AQIThreshold){
                                    Threshold.add(ptr2);
                                }
                            }
                        ptr2 = ptr2.getNext();
                        }
                    }
                    break;
                }
                ptr = ptr.getNext();
            }
        }
        
        return Threshold;
    } 

}
