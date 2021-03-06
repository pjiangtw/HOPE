#include <sys/time.h>
#include <algorithm>
#include <ilcplex/ilocplex.h>
#include <ilcp/cpext.h>
#include <vector>
#include <set>
#include <bitset>
#include <iterator>

// use ILOG's STL namespace
ILOSTLBEGIN
static const int DEFAULT_FILTER_THRESHOLD = 4;
static const int WHEN_GRAPH_FILTERING_IS_BETTER = 5;

IloInt reducedDim          = 0;
IloInt parity_number          = 0;
IloInt parity_minlength       = -1;
IloInt parity_maxlength       = -1;
unsigned long parity_seed;
IloBool parity_use_given_seed = false;
IloInt parity_filterLevel     = 2; 
   // parity_filterLevel = 0: binary representation, individual xors
   // parity_filterLevel = 1: binary representation, Gaussian elimination
   // parity_filterLevel = 2: CP variable representation, individual xors
IloInt parity_filterThreshold = DEFAULT_FILTER_THRESHOLD; 

bool PARITY_DONT_HANDLE_RANDOM_SEED = false;


bool yannakis =true;
bool jaroslow = false;
bool wainr = false;
long short_xor_max_length  = 10;
bool use_pairwise_subs = false;

// global parameters, etc.
unsigned long seed;
bool          use_given_seed = false;
IloInt        timelimit      = -1;
bool          use_tb2        = false;
char          instanceName[1024];

//parity matrix
std::string matrixStr;

//to use external parity matrix
bool externalParity = false;
bool elim = true;
bool generator = false;

unsigned long get_seed(void) {
  struct timeval tv;
  struct timezone tzp;
  gettimeofday(&tv,&tzp);
  return (( tv.tv_sec & 0177 ) * 1000000) + tv.tv_usec;
}

////////////////////////////////

vector <vector <bool> > parseMatrix(int m, int n)
{
  vector <vector <bool> > A;
  if(matrixStr.empty())
    return A;

  int len = matrixStr.length();

  const char* matrixChars = matrixStr.c_str();

  A.resize(m);
  for (int i =0;i<m;i++)
  {
    A[i].resize(n+1);
    A[i][n] = rand()%2==0;     
  }
  int rowCounter=0;
  int colCounter=0;	
  for (int i=0;i<len;i++){
    switch(matrixChars[i]){
      case '1':
        A[rowCounter][colCounter++] = true;
        break;
      case '0':
        A[rowCounter][colCounter++] = false;
        break;
      case '_':
        colCounter=0;
        rowCounter++;
        break;
    }      
  }
  return A;
}

void parseParityArgs(int & argc, char **argv)
{
  // this method eats up all arguments that are relevant for the
  // parity constraint, and returns the rest in argc, argv

  char residualArgv[argc][64];
  strcpy(residualArgv[0], argv[0]);
  int residualArgc = 1;

  for (int argIndex=1; argIndex < argc; ++argIndex) {
    if ( !strcmp(argv[argIndex], "-gen") ) {
      generator=true;
    } else if ( !strcmp(argv[argIndex], "-skipelim") ) {
      elim=false;
    }
    else if ( !strcmp(argv[argIndex], "-matrix") ) {
      argIndex++;
      matrixStr = string(argv[argIndex]);
      externalParity=true;
      //cout<<"here"<<endl;
      //cout<<strcpy(instanceName, argv[argIndex])<<endl;
    }
    else if ( !strcmp(argv[argIndex], "-paritylevel") ) {
      argIndex++;
      parity_filterLevel = atol( argv[argIndex] );
    }
    else if ( !strcmp(argv[argIndex], "-paritythreshold") ) {
      argIndex++;
      parity_filterThreshold = atol( argv[argIndex] );
      if (parity_filterThreshold < 2) {
        cerr << "ERROR: paritythreshold must be at least 2." << endl;
        exit(1);
      }
    }
    else if ( !strcmp(argv[argIndex], "-number") ) {
      argIndex++;
      parity_number = (IloInt)atol( argv[argIndex] );
	  reducedDim = (IloInt)atol( argv[argIndex] );
    }
    else if ( !strcmp(argv[argIndex], "-minlength") ) {
      argIndex++;
      parity_minlength = (IloInt)atol( argv[argIndex] );
    }
	   else if ( !strcmp(argv[argIndex], "-feldman") ) {
		argIndex++;
     short_xor_max_length = atol( argv[argIndex] );
		wainr = true;
	  }
	  	   else if ( !strcmp(argv[argIndex], "-jaroslow") ) {
		argIndex++;
     short_xor_max_length = atol( argv[argIndex] );
		jaroslow = true;
	  }
	  else if ( !strcmp(argv[argIndex], "-yannakis") ) {
		yannakis = true;
	  }
	  	  else if ( !strcmp(argv[argIndex], "-pairwisesubs") ) {
		use_pairwise_subs = true;
	  }
    else if ( !strcmp(argv[argIndex], "-maxlength") ) {
      argIndex++;
      parity_maxlength = (IloInt)atol( argv[argIndex] );
    }
    else if ( !strcmp(argv[argIndex], "-seed") && !PARITY_DONT_HANDLE_RANDOM_SEED ) {
      argIndex++;
      parity_seed =  atol( argv[argIndex] );
      parity_use_given_seed = true;
    }
    else {
      // save this option to be returned back
      strcpy(residualArgv[residualArgc++], argv[argIndex]);
    }
  }

  argc = residualArgc;
  for (int i=1; i<argc; ++i) {
    // free(argv[i]);
    argv[i] = new char[strlen(residualArgv[i])+1];
    strcpy(argv[i], residualArgv[i]);
  }
}

void printParityUsage(ostream & os = cout) {
  os << "ParityConstraint options:" << endl
     << "   -paritylevel        0: binary individual Xors," << endl 
     << "                       1: binary Gaussian elimination, " << endl 
     << "                       2: non-binary individual Xors (default)" << endl
     << "   -paritythreshold    >= 2, for individual Xors (default: 3)" << endl 
     << "   -number             Number of random XORs (default: 0)" << endl
     << "   -minlength          Minlength of XORs (default: nvars/2)" << endl
     << "   -maxlength          Maxlength of XORs (default: nvars/20)" << endl;
  if (!PARITY_DONT_HANDLE_RANDOM_SEED)
    os << "   -seed               Random seed" << endl;
  else {
    os << "   -seed               Disabled; to enable, remove from the model" << endl
       << "                         PARITY_DONT_HANDLE_RANDOM_SEED = true" << endl;
  }
  os << endl;
}


void parseArgs(int argc, char **argv) 
{
  // one argument must be the instance filename
  if (argc <= 1) {
    cerr << "ERROR: instance name must be specified" << endl
         << "       See usage (WH_cpo_uai -h)" << endl;
    exit(1);
  }

  for (int argIndex=1; argIndex < argc; ++argIndex) {
    if ( !strcmp(argv[argIndex], "-tb2") ) {
      use_tb2 = true;
    }
    else if ( !strcmp(argv[argIndex], "-timelimit") ) {
      argIndex++;
      timelimit = atol(argv[argIndex]);
    }
    else if ( !strcmp(argv[argIndex], "-seed") ) {
      argIndex++;
      seed =  atol( argv[argIndex] );
      use_given_seed = true;
    }
    else if ( !strcmp(argv[argIndex], "-verbosity") ) {
      argIndex++;
    }
    else if ( !strcmp(argv[argIndex], "-h") || !strcmp(argv[argIndex], "-help") ) {
      cout << endl
           << "USAGE: iloglue_uai [options] instance.uai" << endl
           << endl
           << "   -timelimit          Timelimit in seconds (default None)" << endl
           << "   -seed               Random seed" << endl
           << endl;
      // print parity constraint options usage
      //printParityUsage(cout);
      exit(0);
    }
    else if (argv[argIndex][0] != '-') {
     // must be the instance name
     strcpy(instanceName, argv[argIndex]);
    }
    else {
      cerr << "ERROR: Unexpected option: " << argv[argIndex] << endl
           << "       See usage (iloglue_uai -h)" << endl;
      exit(1);
    }
  }
}

double median(vector<double> &v)
{
    size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin()+n, v.end());
    return v[n];
}

void print_matrix (vector <vector <bool> > A)
{
for (unsigned int i =0;i<A.size();i++)
	{
		for (unsigned int j =0;j<A[i].size();j++)					// last column is for coefficients b
			cout << A[i][j] << ",";
		cout << endl;
	}
}

vector <vector <bool> > generate_matrix(int m, int n)
{
	vector <vector <bool> > A;
	A.resize(m);
	for (int i =0;i<m;i++)
	{
	A[i].resize(n+1);
	for (int j =0;j<n+1;j++)					// last column is for coefficients b
		//if (rnd_uniform()<0.5)
		if (rand()%2==0)
			A[i][j] = true;
		else
			A[i][j]=false;
	}
	
	// print
	//cout << "Random matrix" <<endl;
	//print_matrix(A);
	
	return A;	
}

vector <bool>  feasiblesol;

void row_echelon(vector <vector <bool> > & A)
{
	bool solvable = true;						// is the system A x + b =0 solvable?
	
	size_t m = A.size();
	size_t n = A[0].size()-1;
	
	vector <int> indep_columns;
		vector <int> indep_columns_rindex;
		set <int> indep_vars;
		
	// put A in row echelon form
	for (size_t i = 0;i<m;i++)
	{
   //Find pivot for column k:
   bool empty_row=true;
   int j_max =0;
	for (int s = 0;s<n;s++)
		if (A[i][s]) 
			{
			empty_row=false;
			j_max = s;
			break;
			}
		if (empty_row)				// low rank
			{
				if (A[i][n])		//0=1
				{
				solvable = false;
				}
				else
				{
				// 0 =0, we can remove this row
				}			
			}
		else
			{
				indep_vars.insert(j_max);
				indep_columns.push_back(j_max);					// index of a basis of A
				indep_columns_rindex.push_back(i);				// row index of pivot
				
				for (size_t h=i+1;h<m;h++)
					if (A[h][j_max])			// if not already zero
						{
							for (int q=0;q<n+1;q++)			// sum the two rows
								A[h][q] = A[h][q] xor A[i][q];
						}
			}

	}
	
	for (size_t i = 0;i<indep_columns.size();i++)
	{
	int j_max = indep_columns[i];
	int p = indep_columns_rindex[i];
	
					for (int h=p-1;h>=0;h--)
					if (A[h][j_max])			// if not already zero
						{
							//print_matrix(A);

							for (int q=0;q<n+1;q++)			// sum the two rows
								A[h][q] = A[h][q] xor A[p][q];
							//cout<<h << " = "<<h <<  "+"<<p << endl;
							//print_matrix(A);
						}
	
	
	}
	
	
	// produce a solution
	

	vector <bool>  b;
	vector <bool>  y;
	feasiblesol.resize(n);
	y.resize(n);
	
	// initialize b to the last column of A
	b.resize(m);
	for (size_t i =0;i<m;i++)
		b[i] = A[i][n];
	
	for (size_t i =0;i<n;i++)
		y[i] = rand()%2;
		
	// sum all the dependent variables that are already fixed
	for (size_t i =0;i<n;i++)	
		{
		feasiblesol[i] = y[i];
		if ( (indep_vars.count(i)==0) && (y[i]==1))		// dependent variable, and non zero
			{				
			// b = b + x[i] * A[] [i]
				for (size_t j =0;j<m;j++)
					b[j] = b[j] xor A[j][i];
			}
		}
		
	/*
	cout << "Printing b" << endl;
	for (size_t j =0;j<m;j++)
		cout << b[j] << ",";
	cout <<  endl;
	*/
	// backsubstitute r

	for (int i =indep_columns_rindex.size()-1;i>=0;i--)
		{
			int c = indep_columns_rindex[i];		// lowest pivot
			if (b[c]==1)		// we need to add a 1
				{
				y[indep_columns[i]] = 1;
				feasiblesol[indep_columns[i]] = 1;
				for (size_t j =0;j<m;j++)
					b[j] = b[j] xor A[j][indep_columns[i]];
				}
			else
			{
				y[indep_columns[i]] = 0;
				feasiblesol[indep_columns[i]] = 0;
			}
		}
	
	
}

//

int sparsify(vector <vector <bool> > & A)
{
vector< bitset<1000> > bv;

bv.resize(A.size());
size_t m = A.size();
size_t n = A[0].size();
int saved_bits = 0;
size_t initialnbr=0;

for (size_t i = 0;i<m;i++)
	{
	bitset<1000> row;
	for (size_t s = 0;s<n;s++)
		if (A[i][s]) 
			{
			row.set(s,1);
			initialnbr++;
			}
	bv[i]=row;	
	}

cout << "Initial # of bits: " << 	initialnbr << endl;


if (m<100)
{
for (size_t i = 0;i<m;i++)
	for (size_t l = 0;l<m;l++)
		for (size_t z = 0;z<m;z++)
		for (size_t g = 0;g<m;g++)
		if (i!=l && i!=z && z!=l && i!=g && z!=g && l!=g)
		{
		int cursize = bv[i].count();
		int newsize =  (bv[i]^bv[l]^bv[z]^bv[g]).count();
		if (newsize<cursize)
			{
			saved_bits = saved_bits + cursize - newsize;
			bv[i]^=bv[l]^bv[z]^bv[g];
			for (int q=0;q<n;q++)			// sum the two rows
				A[i][q] = A[i][q] xor A[l][q] xor A[z][q] xor A[g][q];
			}
		}

}
if (m<500)
{
for (size_t i = 0;i<m;i++)
	for (size_t l = 0;l<m;l++)
		for (size_t z = 0;z<m;z++)
		if (i!=l && i!=z && z!=l)
		{
		int cursize = bv[i].count();
		int newsize =  (bv[i]^bv[l]^bv[z]).count();
		if (newsize<cursize)
			{
			saved_bits = saved_bits + cursize - newsize;
			bv[i]^=bv[l]^bv[z];
			for (int q=0;q<n;q++)			// sum the two rows
				A[i][q] = A[i][q] xor A[l][q] xor A[z][q];
			}
		
		}
}
if (m<10000)
{		
for (size_t i = 0;i<m;i++)
	for (size_t l = 0;l<m;l++)
		if (i!=l)
		{
		int cursize = bv[i].count();
		int newsize =  (bv[i]^bv[l]).count();
		if (newsize<cursize)
			{
			saved_bits = saved_bits + cursize - newsize;
			bv[i]^=bv[l];
		//	print_matrix(A);
			
			for (int q=0;q<n;q++)			// sum the two rows
				A[i][q] = A[i][q] xor A[l][q];
			
			//cout<<i << " = "<<i <<  "+"<<l << endl;
		//	print_matrix(A);
			//cout<<endl;
			}
		
		}
}
/*
for (size_t att = 0;att<10000000;att++)	
{

bitset<1000> rndcomb;
for (size_t i = 0;i<m;i++)
	rndcomb[i] = rand()%2;
	
bitset<1000> res;
for (size_t i = 0;i<m;i++)
	if (rndcomb[i]>0)
		res^=bv[i];

for (size_t i = 0;i<m;i++)
	if (rndcomb[i]>0 && bv[i].count()>res.count())		
		{
		cout << "+";
		saved_bits = saved_bits +bv[i].count() - res.count();
		for (size_t l = 0;l<m;l++)
			if (rndcomb[l]>0 && i!=l)
				{
				for (int q=0;q<n;q++)
					A[i][q] = A[i][q] xor A[l][q];
				bv[i] ^= bv[l];
				}
		break;			// ??
		}
		
}		
*/
cout << "final # of bits: " << 	(int) initialnbr- saved_bits<< endl;		
return saved_bits;	
}

void add_linear_combinations(vector <vector <bool> > & A, size_t M)
{
//size_t cursize = A.size();
//size_t newrindex = 0;

for (size_t i =0;i<M;i++)
	for (size_t k =i;k<M;k++)
		if (k!=i)
			{
			vector <bool> newrow;
			newrow.resize(A[i].size());
			for (size_t j =0;j<A[i].size();j++)
			{
			newrow[j] = A[i][j] xor  A[k][j];
			}
			A.push_back(newrow);
			cout << "adding" << endl;
			}
}

vector <vector <bool> > generate_matrix_maxlength(int m, int n, int k)
{
	vector <vector <bool> > A;
	A.resize(m);
	
	vector <size_t> index;
	index.resize(n);
	for (int j =0;j<n;j++)
		index[j] = j;
			
	
	for (int i =0;i<m;i++)
	{
	A[i].resize(n+1);
	std::random_shuffle(index.begin(), index.end());
	for (int j =0;j<k;j++)					// last column is for coefficients b
		//if (rnd_uniform()<0.5)
		A[i][index[j]] = true;
	}
	
	// fill parity bits at random
	for (int i =0;i<m;i++)
		if (rand()%2==0)
			A[i][n] = true;
		else
			A[i][n] = false;
	// print
	//cout << "Random matrix" <<endl;
	//print_matrix(A);
	
	return A;	
}

vector <vector <bool> > generate_Toeplitz_matrix(int m, int n)
{
	
	vector <vector <bool> > A;
	if (m==0)
		return A;
		
	A.resize(m);
	int i;
	for (i =0;i<m;i++)
	{
	A[i].resize(n+1);
	}
	
	// first column
	for (i =0;i<m;i++)
	{
		if (rand()%2==0)
			A[i][0] = true;
		else
			A[i][0]=false;
		for (int j =1;j<m-i;j++)
			if (j<n)
				A[i+j][j] = A[i][0];
	}
	
		// last column
	for (i =0;i<m;i++)
	{
		if (rand()%2==0)
			A[i][n] = true;
		else
			A[i][n]=false;

	}


	
	// first row
	for (int j =1;j<n;j++)
	{
		if (rand()%2==0)
			A[0][j] = true;
		else
			A[0][j]=false;
			
		for (i =1;i<m;i++)
			if (j+i<n)
				A[i][j+i] = A[0][j];
	}
	
	// print
	//cout << "Random matrix3" <<endl;
	//print_matrix(A);
	
	return A;	
}

typedef std::set<int> set_type;
typedef std::set<set_type> powerset_type;
 
powerset_type powerset2(set_type const& set)
{
  typedef set_type::const_iterator set_iter;
  typedef std::vector<set_iter> vec;
  typedef vec::iterator vec_iter;
 
  struct local
  {
    static int dereference(set_iter v) { return *v; }
  };
 
  powerset_type result;
 
  vec elements;
  do
  {
    set_type tmp;
    std::transform(elements.begin(), elements.end(),
                   std::inserter(tmp, tmp.end()),
                   local::dereference);
    result.insert(tmp);
    if (!elements.empty() && ++elements.back() == set.end())
    {
      elements.pop_back();
    }
    else
    {
      set_iter iter;
      if (elements.empty())
      {
        iter = set.begin();
      }
      else
      {
        iter = elements.back();
        ++iter;
      }
      for (; iter != set.end(); ++iter)
      {
        elements.push_back(iter);
      }
    }
  } while (!elements.empty());
 
  return result;
}

set <set <int> > powerset (set <int> s) {
   set <set <int> > result;
   set <int> nullset; //  the default constructor builds a set with no elements

   /*
   	set <int> ::iterator it4;
		for (it4 = s.begin ( ); it4 != s.end (); it4++)
			cout << (*it4) << " ";
	cout << endl;
	*/		
   if (s.size( ) == 0) {
      result.insert (nullset);
      return result;
   }
   
 //  if (s.size() == k) { result.insert(s); return result; }
   else {
      set <int>::iterator it;
      for (it = s.begin(); it != s.end(); it++) {
         int elem = *it;

         //  copy the original set, and delete one element from it.
         set <int> s1 (s);
         s1.erase (elem);

         //  compute the power set of this smaller set.
         set <set <int> > p1 = powerset (s1);
        
		

		set <set <int> >::iterator it3;
		for (it3 = p1.begin ( ); it3 != p1.end (); it3++) {
		result.insert (*it3);
		}

         //  add the deleted element to each member of this power set,
         //  and insert each new set into the desired result.
         set <set <int> >::iterator iter;
         for (iter = p1.begin(); iter != p1.end(); iter++) {
            set <int>  next = *iter;
			
				next.insert (elem);

				result.insert (next);
				
         };
      };
      return result;
   }
}


IloCP IlogSolver;

// Usage: iloglue problem_name.wcsp [verbosity]
int main(int argc, char **argv)
{
  char pbname[1024];
  int nbvar,nbval,nbconstr;
  IloEnv env;
  IloTimer timer(env);
    
  try {
    // first parse and remove parity-related command-line arguments
    PARITY_DONT_HANDLE_RANDOM_SEED = true;
    parseParityArgs(argc, argv);

    // now parse regular arguments
    parseArgs(argc, argv);

    // associate the CP solver with the environment
    IlogSolver = IloCP(env);

    // associate a model with the environment
    IloModel model(env);
        
    // open the instance file
    ifstream file(instanceName);
    if (!file) {
      cerr << "Could not open file " << instanceName << endl;
      exit(EXIT_FAILURE);
    }
  
    // stefano mod, read uai file
    // reads uai file to parse domain sizes; creates variables along the way
    cerr << "Creating variables"<< endl;
    file >> pbname;
    file >> nbvar;
	IloNumExprArray objexpr(env, nbvar) vars;
    IloIntVarArray newvars(env, nbvar, 0, 100);
    nbval = 0;
    int tmp;
    for (int i=0; i<nbvar; i++) {
      file >> tmp;
      if (tmp>nbval)
        nbval = tmp;
      //vars[i].setBounds(0, tmp-1);									// (17)
      char *name = new char[16];
      sprintf(name, "x%d", i);
      vars[i].setName(name);
    }
	for (int i=0;i<reducedDim;i++){
		newvars[i].setBounds(0, 1);									// (17)
		char *name = new char[16];
        sprintf(name, "y%d", i);
        newvars[i].setName(name);
	}		
	
    model.add(newvars);
	for(int i=0;i<A.size();i++)
		{
			for(int j=0;j<A[0].size();j++)
			{
				
			}
		}
	
	
    file >> nbconstr;
    cerr << "Var:"<< nbvar <<" max dom size:" <<nbval<<" constraints:"<<nbconstr << endl;

    // define variable that captures the value of the objective function
    IloIntVar obj(env, 0, IloIntMax, "objective");


      // use a native CP Optimizer representation of the .uai file
      // read in variable scopes of CPT tables
      std::vector < std::vector< int> > scopes;
      int arity;
      for (int i=0; i<nbconstr; i++) {
        file >> arity;
		std::vector< int> scope;
		int id;
        for (int j=0; j<arity; j++) {
          file >> id;
          scope.push_back(id);
        }
		scopes.push_back(scope);
      }
      // read in values of CPT tables
      IloInt l;
      IloArray<IloNumArray> cost(env);
      int TableSize;
      for (int i=0; i<nbconstr; i++) {
        file >> TableSize;
        double prod;
        IloNum entry;
        IloNumArray table(env);
        for (int j=0; j<TableSize; j++) {
          file >> prod;				
          entry = log10(prod);		// integer vs real
          //entry = 10;		// integer vs real
        //  cout << "adding " << entry << endl;
          table.add(entry);
        }
        cost.add(table);
      }
      cout << "done reading CPTs"<< endl;
      // define cost expression
      IloNumExpr objexpr(env);
	  std::vector < std::vector< std::vector < std::vector< IloBoolVar> > > > Mu;
	  
	  Mu.resize(nbvar);
	  for (size_t q= 0; q<nbvar;q++)
			Mu[q].resize(nbvar);
			
      for (l = 0; l < nbconstr; l++) {        
        IloIntExpr pos(env);			// init to 0
	
	
	
	if (scopes[l].size()==1)
	{
	//objexpr += cost[l][0]* vars[scopes[l][0]]+cost[l][1]* (1-vars[scopes[l][0]]);
	
	if (isfinite(cost[l][0]))
		objexpr += cost[l][0]* (1-vars[scopes[l][0]]);
	else
		{
		if isinf(cost[l][0])
			{
			model.add(vars[scopes[l][0]]==1);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}
	
	if (isfinite(cost[l][1]))
		objexpr += cost[l][1]* (vars[scopes[l][0]]);
	else
		{
		if isinf(cost[l][1])
			{
			model.add(vars[scopes[l][0]]==0);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}
	
	}
	else
	{
			int i = scopes[l][0];
			int j = scopes[l][1];
			
			char *name = new char[32];
			sprintf(name, "mu_%d_%d (0,0)", (int) i, (int) j);
			IloBoolVar mu_i_j_0_0 (env, 0, 1, name);					// (18)
			model.add(mu_i_j_0_0);
			
			sprintf(name, "mu_%d_%d (0,1)", (int) i, (int) j);
			IloBoolVar mu_i_j_0_1 (env, 0, 1, name);					// (18)
			model.add(mu_i_j_0_1);
			
			sprintf(name, "mu_%d_%d (1,0)", (int) i, (int) j);
			IloBoolVar mu_i_j_1_0 (env, 0, 1, name);
			model.add(mu_i_j_1_0);
			
			sprintf(name, "mu_%d_%d (1,1)", (int) i, (int) j);
			IloBoolVar mu_i_j_1_1 (env, 0, 1, name);
			model.add(mu_i_j_1_1);
			
			model.add((mu_i_j_0_0+mu_i_j_1_0 == 1-vars[j]));
			
			model.add((mu_i_j_0_1+mu_i_j_1_1 == vars[j]));
			
			model.add((mu_i_j_0_0+mu_i_j_0_1 == 1-vars[i]));
			
			model.add((mu_i_j_1_0+mu_i_j_1_1 == vars[i]));
			
			
			model.add((mu_i_j_0_1+mu_i_j_1_1 <= 1));
			model.add((mu_i_j_0_0+mu_i_j_1_0 <= 1));
			model.add((mu_i_j_1_0+mu_i_j_1_1 <= 1));
			model.add((mu_i_j_0_0+mu_i_j_0_1 <= 1));
			
			
			Mu[i][j].resize(2);
			Mu[i][j][0].resize(2);
			Mu[i][j][1].resize(2);
			Mu[i][j][0][0]= mu_i_j_0_0 ;
			Mu[i][j][0][1]= mu_i_j_0_1 ;
			Mu[i][j][1][0]= mu_i_j_1_0 ;
			Mu[i][j][1][1]= mu_i_j_1_1 ;
			
		
		
		//objexpr += cost[l][0]* mu_i_j_0_0;
		
		if (isfinite(cost[l][0]))
			objexpr += cost[l][0]* mu_i_j_0_0;
		else
		{
		if isinf(cost[l][0])
			{
			model.add(mu_i_j_0_0==0);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}	
		
		if (isfinite(cost[l][1]))
			objexpr += cost[l][1]* mu_i_j_0_1;
		else
		{
		if isinf(cost[l][1])
			{
			model.add(mu_i_j_0_1==0);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}
		
		if (isfinite(cost[l][2]))
			objexpr +=cost[l][2]* mu_i_j_1_0;
		else
		{
		if isinf(cost[l][2])
			{
			model.add(mu_i_j_1_0==0);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}

		if (isfinite(cost[l][3]))
			objexpr += cost[l][3]* mu_i_j_1_1;	
		else
		{
		if isinf(cost[l][3])
			{
			model.add(mu_i_j_1_1==0);
			}
		else
			{
			cout << "Cannot generate ILP"<< endl;
			exit(-1);
			}
		}

		
				
	}
      }

model.add(IloMaximize(env, objexpr ));

if (!use_given_seed) seed = get_seed();
srand(seed);

// generate matrix of coefficients A x = b. b is the last column

// vector <vector <bool> > A = generate_Toeplitz_matrix(parity_number, nbvar);
// cout << "here" << endl;
// vector <vector <bool> > A = generate_matrix(parity_number, nbvar);

vector <vector <bool> > A;
if(externalParity)
	A = parseMatrix(parity_number, nbvar);
else
	A = generate_Toeplitz_matrix(parity_number, nbvar);

/*
std::vector < std::set <size_t> > varAppearancesInXors;

IloArray<IloArray<IloIntVarArray> > zeta_vars(env);

IloArray <IloIntVarArray> alpha_vars(env);


IloBoolVar dummy_parity (env, 0, 1, "dummy");					// (18)
model.add(dummy_parity);
model.add((dummy_parity==1));
vector <size_t> xors_length;
*/


IloCplex cplex(model);

	if (timelimit > 0)
		cplex.setParam(IloCplex::TiLim, timelimit);
	
	cplex.setParam(IloCplex::Threads, 1);    // number of parallel threads

cout<<"----------------start solving----------------"<<endl;
	cplex.solve();
cout<<"----------------end of solving----------------"<<endl;
     IloNumArray vals(env);
      env.out() << "Solution status = " << cplex.getStatus() << endl;
      env.out() << "Solution value log10lik = " << cplex.getObjValue() << endl;
      env.out() << "number of variables = " << nbvar << endl;

	cplex.getValues(vals, vars);
      env.out() << "Values = " << vals << endl;
	
    } catch (IloException& ex) {
    cout << "Error: " << ex << endl;

  }

	    
   env.end();
  return 0;
}

