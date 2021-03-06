


public class InstanceParams{
	protected boolean logScale=true;
	protected int timeLimit=10;
	protected int retry = 1;
	protected ConstraintType constrained = ConstraintType.UNCONSTRAINED;
	protected double softStrength = 0;
	protected CodeType code=CodeType.DENSE;
	
	public InstanceParams(RunParams parent){
		this.logScale=parent.logScale;
		this.timeLimit=parent.timeLimit;
		this.retry = parent.retry;
		this.constrained = parent.constrained;
		this.softStrength = parent.softStrength;
		this.code=parent.code;
	}
	
	public void setSoftConstrainStrength(double str){
		this.softStrength=str;
	}
	
	public double getSoftConstrainedStrength(){
		return this.softStrength;
	}
	
	public boolean isUnconstrained(){
		return this.constrained==ConstraintType.UNCONSTRAINED;
	}
	
	
	public boolean isDense(){
		return this.code==CodeType.DENSE;
	}
		
	public boolean isRegularPEG(){
		return this.code==CodeType.PEG_REGULAR;
	}
	
	public boolean isPEG(){
		return this.code==CodeType.PEG;
	}
	
	public boolean isLogScale(){
		return this.logScale;
	}
	
	public int getTimeLimit(){
		return this.timeLimit;
	}
	
	public int getNumOfRetry(){
		return this.retry;
	}
}
